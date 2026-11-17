#include "auth_manager.h"

#include <botan/pwdhash.h>
#include <botan/cipher_mode.h>
#include <botan/auto_rng.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cstring>

constexpr size_t SALT_SIZE = 16;
constexpr size_t IV_SIZE = 12;
constexpr size_t KEY_SIZE = 32;

// Parámetros NUEVOS (más fuertes)
static constexpr uint32_t NEW_MEM_KIB = 128 * 1024;  // 128 MiB
static constexpr uint32_t NEW_ITER    = 4;
static constexpr uint32_t NEW_PAR     = 2;

// Parámetros LEGACY (para leer baúles viejos sin header)
static constexpr uint32_t OLD_MEM_KIB = 64 * 1024;
static constexpr uint32_t OLD_ITER    = 3;
static constexpr uint32_t OLD_PAR     = 1;

// Header mágico del formato v2
static const char VAULT_MAGIC[4] = {'R', 'L', 'K', '2'};

static constexpr uint8_t FIELD_SEP = 0x1F;

AuthManager::AuthManager() : logged_in_(false) {
    kdf_ = {NEW_MEM_KIB, NEW_ITER, NEW_PAR};

    const char* home = getenv("HOME");
    std::string base_dir = home ? std::string(home) + "/.local/share/Rubic-Labka" : "/tmp";
    profiles_dir_ = base_dir + "/profiles";
    std::filesystem::create_directories(profiles_dir_);
}

AuthManager::~AuthManager() {
    logout();
}

std::string AuthManager::getVaultPath(const std::string& profile) const {
    return profiles_dir_ + "/" + profile + ".vault";
}

std::vector<std::string> AuthManager::getProfiles() const {
    std::vector<std::string> profiles;
    for (const auto& entry : std::filesystem::directory_iterator(profiles_dir_)) {
        if (entry.is_regular_file() && entry.path().extension() == ".vault") {
            profiles.push_back(entry.path().stem().string());
        }
    }
    return profiles;
}

bool AuthManager::profileExists(const std::string& name) const {
    return std::filesystem::exists(getVaultPath(name));
}

Botan::secure_vector<uint8_t> AuthManager::deriveKey(
    const std::string& password,
    const std::vector<uint8_t>& salt,
    const KdfParams& params) {

    Botan::secure_vector<uint8_t> key(KEY_SIZE);

    auto fam = Botan::PasswordHashFamily::create_or_throw("Argon2id");
    auto pwd = fam->from_params(params.memory_kib, params.iterations, params.parallelism);

    pwd->derive_key(key.data(), key.size(),
                    password.c_str(), password.size(),
                    salt.data(), salt.size());

    return key;
}

bool AuthManager::createProfile(const std::string& name, const std::string& password) {
    if (name.empty() || profileExists(name)) return false;

    Botan::AutoSeeded_RNG rng;
    salt_.resize(SALT_SIZE);
    rng.randomize(salt_.data(), salt_.size());

    kdf_ = {NEW_MEM_KIB, NEW_ITER, NEW_PAR};
    current_key_ = deriveKey(password, salt_, kdf_);

    current_profile_ = name;
    current_vault_path_ = getVaultPath(name);
    logged_in_ = true;

    return saveVault();
}

bool AuthManager::login(const std::string& name, const std::string& password) {
    if (!profileExists(name)) return false;

    current_vault_path_ = getVaultPath(name);

    std::ifstream file(current_vault_path_, std::ios::binary);
    if (!file) return false;

    std::vector<uint8_t> all((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());
    file.close();

    if (all.size() < SALT_SIZE + IV_SIZE + 16) return false;

    const uint8_t* p = all.data();
    size_t n = all.size();

    std::vector<uint8_t> iv(IV_SIZE);
    Botan::secure_vector<uint8_t> ct;

    bool is_v2 = (n >= 4 + 12 + SALT_SIZE + IV_SIZE) &&
                 (std::memcmp(p, VAULT_MAGIC, 4) == 0);

    if (is_v2) {
        // magic(4) + mem(4) + iter(4) + par(4) + salt(16) + iv(12) + ct
        size_t off = 4;
        uint32_t mem, iter, par;
        std::memcpy(&mem,  p + off, 4); off += 4;
        std::memcpy(&iter, p + off, 4); off += 4;
        std::memcpy(&par,  p + off, 4); off += 4;

        kdf_ = {mem, iter, par};

        salt_.assign(p + off, p + off + SALT_SIZE); off += SALT_SIZE;
        iv.assign(p + off, p + off + IV_SIZE);      off += IV_SIZE;
        ct.assign(p + off, p + n);
    } else {
        // Legacy: salt(16) + iv(12) + ct
        kdf_ = {OLD_MEM_KIB, OLD_ITER, OLD_PAR};
        salt_.assign(p, p + SALT_SIZE);
        iv.assign(p + SALT_SIZE, p + SALT_SIZE + IV_SIZE);
        ct.assign(p + SALT_SIZE + IV_SIZE, p + n);
    }

    current_key_ = deriveKey(password, salt_, kdf_);
    logged_in_ = true;

    try {
        auto cipher = Botan::Cipher_Mode::create_or_throw("AES-256/GCM",
                                                          Botan::Cipher_Dir::Decryption);
        cipher->set_key(current_key_);
        cipher->start(iv);
        cipher->finish(ct);
    } catch (...) {
        current_key_.clear();
        logged_in_ = false;
        current_profile_.clear();
        return false;
    }

    if (!deserializeVault(ct)) {
        current_key_.clear();
        logged_in_ = false;
        current_profile_.clear();
        return false;
    }

    current_profile_ = name;
    return true;
}

void AuthManager::logout() {
    current_key_.clear();
    vault_.clear();
    logged_in_ = false;
    current_profile_.clear();
}

void AuthManager::addCredential(const std::string& ip,
                                const std::string& user,
                                const std::string& pass) {
    vault_[ip] = {user, pass};
    saveVault();
}

bool AuthManager::getCredential(const std::string& ip,
                                std::string& user,
                                std::string& pass) {
    auto it = vault_.find(ip);
    if (it != vault_.end()) {
        user = it->second.user;
        pass = it->second.pass;
        return true;
    }
    return false;
}

void AuthManager::removeCredential(const std::string& ip) {
    auto it = vault_.find(ip);
    if (it != vault_.end()) {
        vault_.erase(it);
        saveVault();
    }
}

std::vector<std::string> AuthManager::getSavedIPs() const {
    std::vector<std::string> ips;
    for (const auto& [ip, cred] : vault_) ips.push_back(ip);
    return ips;
}

std::map<std::string, std::string> AuthManager::getPeerNames() const {
    std::map<std::string, std::string> result;
    for (const auto& [ip, cred] : vault_) {
        result[ip] = cred.user.empty() ? ip : cred.user;
    }
    return result;
}

void AuthManager::serializeVault(Botan::secure_vector<uint8_t>& out) {
    auto append_field = [&out](const std::string& s) {
        out.insert(out.end(), s.begin(), s.end());
        out.push_back(FIELD_SEP);
    };

    for (const auto& [ip, cred] : vault_) {
        append_field(ip);
        append_field(cred.user);
        append_field(cred.pass);
        out.push_back('\n');
    }
}

bool AuthManager::deserializeVault(const Botan::secure_vector<uint8_t>& in) {
    vault_.clear();

    std::string data(in.begin(), in.end());
    std::stringstream ss(data);
    std::string line;

    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        std::vector<std::string> parts;
        std::string cur;

        for (unsigned char c : line) {
            if (c == FIELD_SEP) { parts.push_back(cur); cur.clear(); }
            else cur.push_back(static_cast<char>(c));
        }
        if (!cur.empty()) parts.push_back(cur);

        if (parts.size() == 3) {
            if (!parts[0].empty()) vault_[parts[0]] = {parts[1], parts[2]};
        } else if (parts.size() >= 4) {
            // viejo con name: ip,name,user,pass → ignoramos name
            if (!parts[0].empty()) vault_[parts[0]] = {parts[2], parts[3]};
        } else {
            size_t c1 = line.find(':');
            size_t c2 = (c1 == std::string::npos) ? std::string::npos : line.find(':', c1 + 1);
            if (c1 != std::string::npos && c2 != std::string::npos) {
                vault_[line.substr(0, c1)] = {line.substr(c1 + 1, c2 - c1 - 1),
                                              line.substr(c2 + 1)};
            }
        }
    }

    return true;
}

bool AuthManager::saveVault() {
    if (!logged_in_ || current_vault_path_.empty()) return false;

    Botan::secure_vector<uint8_t> plain;
    serializeVault(plain);

    Botan::AutoSeeded_RNG rng;
    std::vector<uint8_t> iv(IV_SIZE);
    rng.randomize(iv.data(), iv.size());

    auto cipher = Botan::Cipher_Mode::create_or_throw("AES-256/GCM",
                                                      Botan::Cipher_Dir::Encryption);
    cipher->set_key(current_key_);
    cipher->start(iv);

    Botan::secure_vector<uint8_t> ct = plain;
    cipher->finish(ct);

    std::ofstream file(current_vault_path_, std::ios::binary | std::ios::trunc);
    if (!file) return false;

    // Header v2: magic + params + salt + iv + ct
    file.write(VAULT_MAGIC, 4);
    file.write(reinterpret_cast<const char*>(&kdf_.memory_kib), 4);
    file.write(reinterpret_cast<const char*>(&kdf_.iterations), 4);
    file.write(reinterpret_cast<const char*>(&kdf_.parallelism), 4);
    file.write(reinterpret_cast<const char*>(salt_.data()), SALT_SIZE);
    file.write(reinterpret_cast<const char*>(iv.data()), IV_SIZE);
    file.write(reinterpret_cast<const char*>(ct.data()), ct.size());

    return true;
}

bool AuthManager::loadVault() {
    // La lectura real ocurre en login(); este método se conserva por compatibilidad.
    return logged_in_;
}