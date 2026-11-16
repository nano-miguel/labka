// src/core/ssh_client.h
#ifndef SSH_CLIENT_H
#define SSH_CLIENT_H

#include <string>
#include <vector>
#include <libssh/libssh.h>
#include <libssh/sftp.h>

class SSHClient {
  public:
    SSHClient();
    ~SSHClient();

    bool connect(const std::string& ip, const std::string& user, const std::string& pass, std::string& error_out);
    void disconnect();
    bool isConnected() const { return connected_; }

    std::vector<std::string> listDir(const std::string& path);
    bool downloadFile(const std::string& remote, const std::string& local);

  private:
    ssh_session session_;
    sftp_session sftp_;
    bool connected_;
};

#endif // SSH_CLIENT_H