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
constexpr size_t GCM_TAG_SIZE = 16;

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

// ─── Helpers de endianness (formato portable LE) ───────────────────
static void writeU32LE(uint8_t* dst, uint32_t v) {
    dst[0] = static_cast<uint8_t>((v      ) & 0xFF);
    dst[1] = static_cast<uint8_t>((v >>  8) & 0xFF);
    dst[2] = static_cast<uint8_t>((v >> 16) & 0xFF);
    dst[3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

static uint32_t readU32LE(const uint8_t* src) {
    return  static_cast<uint32_t>(src[0])
         | (static_cast<uint32_t>(src[1]) <<  8)
         | (static_cast<uint32_t>(src[2]) << 16)
         | (static_cast<uint32_t>(src[3]) << 24);
}

// v2 layout: magic(4) + mem(4) + iter(4) + par(4) + salt(16) + iv(12) + ct(N)
static constexpr size_t V2_HEADER_SIZE = 4 + 4 + 4 + 4 + SALT_SIZE + IV_SIZE;
// v1 layout: salt(16) + iv(12) + ct(N)
static constexpr size_t V1_HEADER_SIZE = SALT_SIZE + IV_SIZE;

// ─── Validación de nombres de perfil ───────────────────────────────
bool AuthManager::isValidProfileName(const std::string& name) {
    if (name.empty() || name.size() > 128) return false;
    for (unsigned char c : name) {
        const bool ok =
            (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '_' || c == '-' || c == '.';
        if (!ok) return false;
    }
    // Sigue bloqueando traversal: si contiene '/' o '\' ya ha salido arriba,
    // pero doblamos la apuesta aquí por claridad.
    if (name.find('/')  != std::string::npos) return false;
    if (name.find('\\') != std::string::npos) return false;
    return true;
}

// ─── Ciclo de vida ─────────────────────────────────────────────────
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

// ─── Rutas de perfil ───────────────────────────────────────────────
std::string AuthManager::getVaultPath(const std::string& profile) const {
    // Defensa en profundidad: si el nombre no es válido, no hay path.
    if (!isValidProfileName(profile)) return std::string();
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
    const std::string p = getVaultPath(name);
    if (p.empty()) return false;
    return std::filesystem::exists(p);
}

// ─── KDF ───────────────────────────────────────────────────────────
Botan::secure_vector<uint8_t> AuthManager::deriveKey(
    const std::string& password,
    const std::vector<uint8_t>& salt,
    const KdfParams& params) {

    Botan::secure_vector<uint8_t> key(KEY_SIZE);

    auto fam = Botan::PasswordHashFamily::create_or_throw("Argon2id");
    auto pwd = fam->from_params(params.memory_kib, params.iterations, params.parallelism);

    pwd->derive_key(key.data(), key.size(),
        password.c_str(),
        password.size(),
        salt.data(), salt.size());

    return key;
}

// ─── Crear perfil ──────────────────────────────────────────────────
bool AuthManager::createProfile(
    const std::string& name,
    const std::string& password) {

    if (!isValidProfileName(name)) return false;
    if (profileExists(name)) return false;

    Botan::AutoSeeded_RNG rng;
    salt_.resize(SALT_SIZE);
    rng.randomize(salt_.data(), salt_.size());

    kdf_ = {NEW_MEM_KIB, NEW_ITER, NEW_PAR};
    current_key_ = deriveKey(password, salt_, kdf_);

    current_profile_   = name;
    current_vault_path_ = getVaultPath(name);
    if (current_vault_path_.empty()) {
        // No debería ocurrir tras isValidProfileName, pero cortamos por si acaso.
        current_key_.clear();
        salt_.clear();
        current_profile_.clear();
        return false;
    }
    logged_in_ = true;

    return saveVault();
}

// ─── Login ─────────────────────────────────────────────────────────
bool AuthManager::login(
    const std::string& name,
    const std::string& password) {

    if (!profileExists(name)) return false;

    current_vault_path_ = getVaultPath(name);
    if (current_vault_path_.empty()) return false;

    std::ifstream file(current_vault_path_, std::ios::binary);
    if (!file) return false;

    std::vector<uint8_t> all((
        std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>());
    file.close();

    // Mínimo: cabecera legacy + tag GCM
    if (all.size() < V1_HEADER_SIZE + GCM_TAG_SIZE) return false;

    const uint8_t* p = all.data();
    const size_t   n = all.size();

    std::vector<uint8_t> iv(IV_SIZE);
    Botan::secure_vector<uint8_t> ct;

    const bool is_v2 = (n >= V2_HEADER_SIZE + GCM_TAG_SIZE) &&
                       (std::memcmp(p, VAULT_MAGIC, 4) == 0);

    if (is_v2) {
        // magic(4) + mem(4) + iter(4) + par(4) + salt(16) + iv(12) + ct
        size_t off = 4;
        kdf_.memory_kib  = readU32LE(p + off); off += 4;
        kdf_.iterations  = readU32LE(p + off); off += 4;
        kdf_.parallelism = readU32LE(p + off); off += 4;

        salt_.assign(p + off, p + off + SALT_SIZE); off += SALT_SIZE;
        iv.assign   (p + off, p + off + IV_SIZE);   off += IV_SIZE;
        ct.assign   (p + off, p + n);
    } else {
        // Legacy: salt(16) + iv(12) + ct
        kdf_ = {OLD_MEM_KIB, OLD_ITER, OLD_PAR};
        salt_.assign(p, p + SALT_SIZE);
        iv.assign   (p + SALT_SIZE, p + SALT_SIZE + IV_SIZE);
        ct.assign   (p + SALT_SIZE + IV_SIZE, p + n);
    }

    current_key_ = deriveKey(password, salt_, kdf_);
    logged_in_ = true;

    try {
        auto cipher = Botan::Cipher_Mode::create_or_throw(
            "AES-256/GCM", Botan::Cipher_Dir::Decryption);
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
    current_vault_path_.clear();
    salt_.clear();
}

// ─── Credenciales ──────────────────────────────────────────────────
void AuthManager::addCredential(
    const std::string& ip,
    const std::string& user,
    const std::string& pass) {

    vault_[ip] = {user, pass};
    saveVault();
}

bool AuthManager::getCredential(
    const std::string& ip,
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

// ─── Serialización ─────────────────────────────────────────────────
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
                vault_[line.substr(0, c1)] = {
                    line.substr(c1 + 1, c2 - c1 - 1),
                    line.substr(c2 + 1)
                };
            }
        }
    }

    return true;
}

// ─── Guardar ───────────────────────────────────────────────────────
bool AuthManager::saveVault() {
    if (!logged_in_ || current_vault_path_.empty()) return false;
    if (salt_.size() != SALT_SIZE)                   return false;

    Botan::secure_vector<uint8_t> plain;
    serializeVault(plain);

    Botan::AutoSeeded_RNG rng;
    std::vector<uint8_t> iv(IV_SIZE);
    rng.randomize(iv.data(), iv.size());

    auto cipher = Botan::Cipher_Mode::create_or_throw(
        "AES-256/GCM", Botan::Cipher_Dir::Encryption);
    cipher->set_key(current_key_);
    cipher->start(iv);

    Botan::secure_vector<uint8_t> ct = plain;
    cipher->finish(ct);

    // Escritura atómica: escribimos a .tmp y renombramos al final.
    const std::string tmp_path = current_vault_path_ + ".tmp";
    std::ofstream file(tmp_path, std::ios::binary | std::ios::trunc);
    if (!file) return false;

    // Header v2: magic(4) + mem(4) + iter(4) + par(4) + salt(16) + iv(12) + ct(N)
    uint8_t magic[4] = {VAULT_MAGIC[0], VAULT_MAGIC[1], VAULT_MAGIC[2], VAULT_MAGIC[3]};
    uint8_t enc_mem[4], enc_iter[4], enc_par[4];
    writeU32LE(enc_mem,  kdf_.memory_kib);
    writeU32LE(enc_iter, kdf_.iterations);
    writeU32LE(enc_par,  kdf_.parallelism);

    file.write(reinterpret_cast<const char*>(magic),    4);
    file.write(reinterpret_cast<const char*>(enc_mem),  4);
    file.write(reinterpret_cast<const char*>(enc_iter), 4);
    file.write(reinterpret_cast<const char*>(enc_par),  4);
    file.write(reinterpret_cast<const char*>(salt_.data()), SALT_SIZE);   // ← FIX: antes faltaba
    file.write(reinterpret_cast<const char*>(iv.data()),    IV_SIZE);
    file.write(reinterpret_cast<const char*>(ct.data()),    ct.size());

    file.flush();
    if (!file) {
        file.close();
        std::error_code ec;
        std::filesystem::remove(tmp_path, ec);
        return false;
    }
    file.close();

    std::error_code ec;
    std::filesystem::rename(tmp_path, current_vault_path_, ec);
    if (ec) {
        std::filesystem::remove(tmp_path, ec);
        return false;
    }
    return true;
}

bool AuthManager::loadVault() {
    // La lectura real ocurre en login(); este método se conserva por compatibilidad.
    return logged_in_;
}