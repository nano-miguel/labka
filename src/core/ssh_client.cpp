// src/core/ssh_client.cpp
#include "ssh_client.h"

#include <iostream>
#include <cstring>
#include <cstdio>
#include <fcntl.h>

SSHClient::SSHClient()
    : session_(nullptr), sftp_(nullptr), connected_(false) {}

SSHClient::~SSHClient() {
    disconnect();
}

bool SSHClient::connect(const std::string& ip,
                        const std::string& user,
                        const std::string& pass,
                        std::string& error_out) {
    std::lock_guard<std::recursive_mutex> lk(mtx_);

    if (connected_) disconnect();

    session_ = ssh_new();
    if (!session_) {
        error_out = "No se pudo crear la sesión SSH.";
        return false;
    }

    ssh_options_set(session_, SSH_OPTIONS_HOST, ip.c_str());
    ssh_options_set(session_, SSH_OPTIONS_USER, user.c_str());

    long timeout_sec = 5;
    ssh_options_set(session_, SSH_OPTIONS_TIMEOUT, &timeout_sec);

    if (ssh_connect(session_) != SSH_OK) {
        error_out = "Fallo de conexión: " + std::string(ssh_get_error(session_));
        ssh_free(session_);
        session_ = nullptr;
        return false;
    }

    int auth_rc = SSH_AUTH_ERROR;

    if (!pass.empty()) {
        auth_rc = ssh_userauth_password(session_, nullptr, pass.c_str());
    }

    if (auth_rc != SSH_AUTH_SUCCESS) {
        const char* username_ptr = user.empty() ? nullptr : user.c_str();
        auth_rc = ssh_userauth_publickey_auto(session_, username_ptr, nullptr);
    }

    if (auth_rc != SSH_AUTH_SUCCESS) {
        error_out = "Fallo de autenticación: " + std::string(ssh_get_error(session_));
        ssh_disconnect(session_);
        ssh_free(session_);
        session_ = nullptr;
        return false;
    }

    connected_ = true;
    return true;
}

void SSHClient::disconnect() {
    std::lock_guard<std::recursive_mutex> lk(mtx_);

    if (sftp_) {
        sftp_free(sftp_);
        sftp_ = nullptr;
    }

    if (session_) {
        ssh_disconnect(session_);
        ssh_free(session_);
        session_ = nullptr;
    }

    connected_ = false;
}

// ★ Keepalive barato: SSH_MSG_IGNORE. Si falla, la sesión murió.
bool SSHClient::isAlive() {
    std::lock_guard<std::recursive_mutex> lk(mtx_);

    if (!connected_ || !session_) return false;

    if (ssh_send_ignore(session_, "labka-keepalive") == SSH_ERROR) {
        connected_ = false;
        return false;
    }

    return true;
}

std::vector<std::string> SSHClient::listDir(const std::string& path) {
    std::lock_guard<std::recursive_mutex> lk(mtx_);

    std::vector<std::string> files;
    if (!connected_) return files;

    if (!sftp_) {
        sftp_ = sftp_new(session_);
        if (!sftp_ || sftp_init(sftp_) != SSH_OK) {
            if (sftp_) { sftp_free(sftp_); sftp_ = nullptr; }
            return files;
        }
    }

    sftp_dir dir = sftp_opendir(sftp_, path.c_str());
    if (!dir) return files;

    sftp_attributes attr;
    while ((attr = sftp_readdir(sftp_, dir)) != nullptr) {
        if (attr->name) {
            std::string name(attr->name);
            if (name != "." && name != "..") files.push_back(name);
        }
        sftp_attributes_free(attr);
    }

    sftp_closedir(dir);
    return files;
}

bool SSHClient::downloadFile(const std::string& remote, const std::string& local) {
    std::lock_guard<std::recursive_mutex> lk(mtx_);

    if (!connected_) return false;

    if (!sftp_) {
        sftp_ = sftp_new(session_);
        if (!sftp_ || sftp_init(sftp_) != SSH_OK) {
            if (sftp_) { sftp_free(sftp_); sftp_ = nullptr; }
            return false;
        }
    }

    sftp_file file = sftp_open(sftp_, remote.c_str(), O_RDONLY, 0);
    if (!file) return false;

    FILE* fp = fopen(local.c_str(), "wb");
    if (!fp) { sftp_close(file); return false; }

    char buffer[32768];
    int nbytes;
    bool success = true;

    while ((nbytes = sftp_read(file, buffer, sizeof(buffer))) > 0) {
        if (fwrite(buffer, 1, nbytes, fp) != (size_t)nbytes) { success = false; break; }
    }

    if (nbytes < 0) success = false;

    fclose(fp);
    sftp_close(file);
    return success;
}

bool SSHClient::executeCommand(const std::string& command,
                               std::string& output_out,
                               std::string& error_out) {
    std::lock_guard<std::recursive_mutex> lk(mtx_);

    output_out.clear();
    error_out.clear();

    if (!connected_ || !session_) {
        error_out = "No hay conexión SSH activa.";
        return false;
    }

    ssh_channel channel = ssh_channel_new(session_);
    if (!channel) {
        error_out = ssh_get_error(session_);
        return false;
    }

    int rc = ssh_channel_open_session(channel);
    if (rc != SSH_OK) {
        error_out = ssh_get_error(session_);
        ssh_channel_free(channel);
        connected_ = false;
        return false;
    }

    rc = ssh_channel_request_exec(channel, command.c_str());
    if (rc != SSH_OK) {
        error_out = ssh_get_error(session_);
        ssh_channel_close(channel);
        ssh_channel_free(channel);
        connected_ = false;
        return false;
    }

    char buffer[4096];
    int nbytes = 0;
    std::string out;

    while ((nbytes = ssh_channel_read(channel, buffer, sizeof(buffer), 0)) > 0) {
        out.append(buffer, nbytes);
    }

    if (nbytes == SSH_ERROR) {
        error_out = ssh_get_error(session_);
        ssh_channel_close(channel);
        ssh_channel_free(channel);
        connected_ = false;
        return false;
    }

    int status = ssh_channel_get_exit_status(channel);
    if (status == SSH_ERROR) status = 0;

    ssh_channel_send_eof(channel);
    ssh_channel_close(channel);
    ssh_channel_free(channel);

    output_out = out;

    if (status != 0) {
        error_out = output_out.empty()
            ? ("El comando terminó con código " + std::to_string(status))
            : output_out;
        return false;
    }

    return true;
}