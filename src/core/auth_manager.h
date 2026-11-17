#ifndef AUTH_MANAGER_H
#define AUTH_MANAGER_H

#include <string>
#include <map>
#include <vector>
#include <cstdint>
#include <botan/secmem.h>

class AuthManager {
public:
    AuthManager();
    ~AuthManager();

    std::vector<std::string> getProfiles() const;
    bool profileExists(
        const std::string& name) const;
    bool createProfile(
        const std::string& name,
        const std::string& password);
    bool login(
        const std::string& name,
        const std::string& password);
    void logout();

    bool isLoggedIn() const {
        return logged_in_; }
    std::string getCurrentProfile() const {
        return current_profile_; }

    void addCredential(
        const std::string& ip,
        const std::string& user,
        const std::string& pass);

    bool getCredential(
        const std::string& ip,
        std::string& user,
        std::string& pass);

    void removeCredential(const std::string& ip);
    std::vector<std::string> getSavedIPs() const;

    // ip -> usuario SSH (se renderiza como línea de credencial, no como nombre)
    std::map<std::string, std::string> getPeerNames() const;

private:
    struct Credential {
        std::string user;
        std::string pass;
    };

    // Parámetros Argon2id versionados (se guardan en el vault)
    struct KdfParams {
        uint32_t memory_kib;
        uint32_t iterations;
        uint32_t parallelism;
    };

    std::map<std::string, Credential> vault_;

    std::string profiles_dir_;
    std::string current_profile_;
    std::string current_vault_path_;

    Botan::secure_vector<uint8_t> current_key_;
    std::vector<uint8_t> salt_;
    KdfParams kdf_;
    bool logged_in_;

    Botan::secure_vector<uint8_t> deriveKey(
        const std::string& password,
        const std::vector<uint8_t>& salt,
        const KdfParams& params);

    bool saveVault();
    bool loadVault();

    void serializeVault(Botan::secure_vector<uint8_t>& out);
    bool deserializeVault(
        const Botan::secure_vector<uint8_t>& in);

    std::string getVaultPath(
        const std::string& profile) const;

    // NUEVO: validación de nombre de perfil (anti path-traversal)
    static bool isValidProfileName(const std::string& name);
};

#endif // AUTH_MANAGER_H