// src/core/ssh_client.h
#ifndef SSH_CLIENT_H
#define SSH_CLIENT_H

#include <string>
#include <vector>
#include <mutex>
#include <atomic>

#include <libssh/libssh.h>
#include <libssh/sftp.h>

class SSHClient {
public:
    SSHClient();
    ~SSHClient();

    bool connect(const std::string& ip,
                 const std::string& user,
                 const std::string& pass,
                 std::string& error_out);

    void disconnect();

    bool isConnected() const { return connected_.load(); }

    // Keepalive: detecta si la sesión realmente sigue viva
    bool isAlive();

    std::vector<std::string> listDir(const std::string& path);
    bool downloadFile(const std::string& remote, const std::string& local);

    bool executeCommand(const std::string& command,
                        std::string& output_out,
                        std::string& error_out);

private:
    ssh_session session_;
    sftp_session sftp_;
    std::atomic<bool> connected_;

    // El watchdog y la UI usan el mismo cliente → protegido
    mutable std::recursive_mutex mtx_;
};

#endif // SSH_CLIENT_H