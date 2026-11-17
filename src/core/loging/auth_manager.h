#ifndef AUTH_MANAGER_H
#define AUTH_MANAGER_H

#include <string>
#include <map>
#include <vector>
#include <botan/secmem.h>

class AuthManager {
  public:
    AuthManager();
    ~AuthManager();

    bool hasMasterUser() const;
    bool createMasterUser(const std::string& password);
    bool login(const std::string& password);
    void logout();
    bool isLoggedIn() const { return logged_in_; }

    void addCredential(const std::string& ip, const std::string& user, const std::string& pass);
    bool getCredential(const std::string& ip, std::string& user, std::string& pass);

  private:
    struct Credential {
        std::string user;
        std::string pass;
    };

    std::map<std::string, Credential> vault_;
    std::string vault_path_;
    Botan::secure_vector<uint8_t> current_key_;
    bool logged_in_;

    Botan::secure_vector<uint8_t> deriveKey(const std::string& password, const std::vector<uint8_t>& salt);
    bool saveVault();
    bool loadVault();
    void serializeVault(Botan::secure_vector<uint8_t>& out);
    bool deserializeVault(const Botan::secure_vector<uint8_t>& in);
};

#endif // AUTH_MANAGER_H
