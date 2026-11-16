// src/core/ssh_client.cpp
#include "ssh_client.h"
#include <iostream>
#include <cstring>
#include <fcntl.h>

SSHClient::SSHClient() : session_(nullptr), sftp_(nullptr), connected_(false) {}

SSHClient::~SSHClient() { disconnect(); }

bool SSHClient::connect(const std::string& ip, const std::string& user, const std::string& pass, std::string& error_out) {
    if (connected_) disconnect();
    session_ = ssh_new();
    if (!session_) { error_out = "No se pudo crear la sesión SSH."; return false; }

    ssh_options_set(session_, SSH_OPTIONS_HOST, ip.c_str());
    ssh_options_set(session_, SSH_OPTIONS_USER, user.c_str());
    
    // C++ requiere una variable para pasar el puntero
    long timeout_sec = 5;
    ssh_options_set(session_, SSH_OPTIONS_TIMEOUT, &timeout_sec);

    if (ssh_connect(session_) != SSH_OK) {
        error_out = "Fallo de conexión: " + std::string(ssh_get_error(session_));
        ssh_free(session_); session_ = nullptr; return false;
    }
    if (ssh_userauth_password(session_, nullptr, pass.c_str()) != SSH_AUTH_SUCCESS) {
        error_out = "Fallo de autenticación: " + std::string(ssh_get_error(session_));
        ssh_disconnect(session_); ssh_free(session_); session_ = nullptr; return false;
    }
    connected_ = true;
    return true;
}

void SSHClient::disconnect() {
    if (sftp_) { sftp_free(sftp_); sftp_ = nullptr; }
    if (session_) {
        ssh_disconnect(session_);
        ssh_free(session_);
        session_ = nullptr;
    }
    connected_ = false;
}

std::vector<std::string> SSHClient::listDir(const std::string& path) {
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