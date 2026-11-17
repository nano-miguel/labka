#include "auth_manager.h"
#include <botan/pwdhash.h>
#include <botan/cipher_mode.h>
#include <botan/auto_rng.h>
#include <filesystem>
#include <fstream>
#include <sstream>

constexpr size_t SALT_SIZE = 16;
constexpr size_t IV_SIZE = 12;
constexpr size_t KEY_SIZE = 32;
constexpr size_t ARGON2_MEMORY = 64 * 1024;
constexpr size_t ARGON2_ITERATIONS = 3;
constexpr size_t ARGON2_PARALLELISM = 1;

AuthManager::AuthManager() : logged_in_(false) {
    const char* home = getenv("HOME");
    std::string base_dir = home ? std::string(home) + "/.local/share/Rubic-Labka" : "/tmp";
    std::filesystem::create_directories(base_dir);
    vault_path_ = base_dir + "/auth.vault";
}

AuthManager::~AuthManager() {
    logout();
}

bool AuthManager::hasMasterUser() const {
    return std::filesystem::exists(vault_path_);
}

Botan::secure_vector<uint8_t> AuthManager::deriveKey(const std::string& password, const std::vector<uint8_t>& salt) {
    Botan::secure_vector<uint8_t> key(KEY_SIZE);
    auto pwdhash_fam = Botan::PasswordHashFamily::create_or_throw("Argon2id");
    auto pwdhash = pwdhash_fam->from_params(ARGON2_MEMORY, ARGON2_ITERATIONS, ARGON2_PARALLELISM);
    pwdhash->derive_key(key.data(), key.size(), password.c_str(), password.size(), salt.data(), salt.size());
    return key;
}

bool AuthManager::createMasterUser(const std::string& password) {
    if (hasMasterUser()) return false;
    Botan::AutoSeeded_RNG rng;
    std::vector<uint8_t> salt(SALT_SIZE);
    rng.randomize(salt.data(), salt.size());
    
    current_key_ = deriveKey(password, salt);
    logged_in_ = true;
    
    // Guardar un baúl vacío
    return saveVault();
}

bool AuthManager::login(const std::string& password) {
    if (!hasMasterUser()) return false;

    std::ifstream file(vault_path_, std::ios::binary);
    if (!file) return false;

    std::vector<uint8_t> salt(SALT_SIZE);
    file.read(reinterpret_cast<char*>(salt.data()), SALT_SIZE);

    current_key_ = deriveKey(password, salt);
    logged_in_ = true;

    // Intentar cargar el baúl para verificar la contraseña
    if (!loadVault()) {
        current_key_.clear();
        logged_in_ = false;
        return false;
    }
    return true;
}

void AuthManager::logout() {
    current_key_.clear();
    vault_.clear();
    logged_in_ = false;
}

void AuthManager::addCredential(const std::string& ip, const std::string& user, const std::string& pass) {
    vault_[ip] = {user, pass};
    saveVault();
}

bool AuthManager::getCredential(const std::string& ip, std::string& user, std::string& pass) {
    auto it = vault_.find(ip);
    if (it != vault_.end()) {
        user = it->second.user;
        pass = it->second.pass;
        return true;
    }
    return false;
}

void AuthManager::serializeVault(Botan::secure_vector<uint8_t>& out) {
    for (const auto& pair : vault_) {
        out.insert(out.end(), pair.first.begin(), pair.first.end()); out.push_back(':');
        out.insert(out.end(), pair.second.user.begin(), pair.second.user.end()); out.push_back(':');
        out.insert(out.end(), pair.second.pass.begin(), pair.second.pass.end()); out.push_back('\n');
    }
}

bool AuthManager::deserializeVault(const Botan::secure_vector<uint8_t>& in) {
    vault_.clear();
    std::string data(in.begin(), in.end());
    std::stringstream ss(data);
    std::string line;
    while (std::getline(ss, line)) {
        size_t c1 = line.find(':');
        size_t c2 = line.find(':', c1 + 1);
        if (c1 != std::string::npos && c2 != std::string::npos) {
            std::string ip = line.substr(0, c1);
            std::string user = line.substr(c1 + 1, c2 - c1 - 1);
            std::string pass = line.substr(c2 + 1);
            vault_[ip] = {user, pass};
        }
    }
    return true;
}

bool AuthManager::saveVault() {
    if (!logged_in_) return false;

    Botan::secure_vector<uint8_t> plain;
    serializeVault(plain);

    Botan::AutoSeeded_RNG rng;
    std::vector<uint8_t> iv(IV_SIZE);
    rng.randomize(iv.data(), iv.size());

    auto cipher = Botan::Cipher_Mode::create_or_throw("AES-256/GCM", Botan::Cipher_Dir::Encryption);
    cipher->set_key(current_key_);
    cipher->start(iv);

    Botan::secure_vector<uint8_t> ct = plain;
    cipher->finish(ct);

    std::ofstream file(vault_path_, std::ios::binary | std::ios::trunc);
    if (!file) return false;

    // El archivo tiene: [SALT guardado previamente] + [IV] + [Ciphertext]
    // (Nota: Para simplificar, el salt se lee en loadVault y se reescribe al inicio)
    std::ifstream existing(vault_path_, std::ios::binary);
    std::vector<uint8_t> salt(SALT_SIZE);
    if (existing) existing.read(reinterpret_cast<char*>(salt.data()), SALT_SIZE);
    
    file.write(reinterpret_cast<const char*>(salt.data()), SALT_SIZE);
    file.write(reinterpret_cast<const char*>(iv.data()), IV_SIZE);
    file.write(reinterpret_cast<const char*>(ct.data()), ct.size());
    return true;
}

bool AuthManager::loadVault() {
    std::ifstream file(vault_path_, std::ios::binary);
    if (!file) return false;

    std::vector<uint8_t> salt(SALT_SIZE);
    file.read(reinterpret_cast<char*>(salt.data()), SALT_SIZE);
    file.seekg(0, std::ios::end);
    size_t total_size = file.tellg();
    file.seekg(SALT_SIZE, std::ios::beg);

    if (total_size <= SALT_SIZE + IV_SIZE) return false;

    std::vector<uint8_t> iv(IV_SIZE);
    file.read(reinterpret_cast<char*>(iv.data()), IV_SIZE);

    Botan::secure_vector<uint8_t> ct(total_size - SALT_SIZE - IV_SIZE);
    file.read(reinterpret_cast<char*>(ct.data()), ct.size());

    try {
        auto cipher = Botan::Cipher_Mode::create_or_throw("AES-256/GCM", Botan::Cipher_Dir::Decryption);
        cipher->set_key(current_key_);
        cipher->start(iv);
        cipher->finish(ct);
        return deserializeVault(ct);
    } catch (...) {
        return false;
    }
}
