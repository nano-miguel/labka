// src/core/labka_core.cpp
#include "labka_core.h"
#include <iostream>
#include <sstream>
#include <cstring>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <ifaddrs.h>
#include <netdb.h>

LabkaCore::LabkaCore() {
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) my_hostname_ = std::string(hostname);
    else my_hostname_ = "Labka-PC";

    struct ifaddrs *ifaddr, *ifa;
    if (getifaddrs(&ifaddr) == 0) {
        for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
            if (ifa->ifa_addr == nullptr) continue;
            if (ifa->ifa_addr->sa_family == AF_INET) {
                char addr[NI_MAXHOST];
                if (getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in), addr, NI_MAXHOST, nullptr, 0, NI_NUMERICHOST) == 0) {
                    if (std::string(addr) != "127.0.0.1") { my_ip_ = std::string(addr); break; }
                }
            }
        }
        freeifaddrs(ifaddr);
    }
    if (my_ip_.empty()) my_ip_ = "127.0.0.1";
}

LabkaCore::~LabkaCore() { stop(); }

void LabkaCore::start(int tcp_port) {
    if (running_) return;
    tcp_port_ = tcp_port;
    running_ = true;

    udp_sock_ = socket(AF_INET, SOCK_DGRAM, 0);
    int broadcast = 1;
    setsockopt(udp_sock_, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));
    setsockopt(udp_sock_, SOL_SOCKET, SO_REUSEADDR, &broadcast, sizeof(broadcast));

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(2123);
    bind(udp_sock_, (struct sockaddr*)&addr, sizeof(addr));

    bcast_thread_ = std::thread(&LabkaCore::radarBroadcastLoop, this);
    listen_thread_ = std::thread(&LabkaCore::radarListenLoop, this);
}

void LabkaCore::stop() {
    running_ = false;
    if (udp_sock_ >= 0) {
        shutdown(udp_sock_, SHUT_RDWR);
        close(udp_sock_);
        udp_sock_ = -1;
    }
    if (bcast_thread_.joinable()) bcast_thread_.join();
    if (listen_thread_.joinable()) listen_thread_.join();
}

void LabkaCore::radarBroadcastLoop() {
    struct sockaddr_in bcast_addr;
    bcast_addr.sin_family = AF_INET;
    bcast_addr.sin_port = htons(2123);
    bcast_addr.sin_addr.s_addr = inet_addr("255.255.255.255");

    while (running_) {
        std::string payload = "LABKA_PING|" + my_hostname_ + "|" + my_ip_;
        sendto(udp_sock_, payload.c_str(), payload.size(), 0, (struct sockaddr*)&bcast_addr, sizeof(bcast_addr));
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }
}

void LabkaCore::radarListenLoop() {
    char buffer[1024];
    while (running_) {
        struct sockaddr_in cli_addr;
        socklen_t cli_len = sizeof(cli_addr);
        ssize_t n = recvfrom(udp_sock_, buffer, sizeof(buffer) - 1, 0, (struct sockaddr*)&cli_addr, &cli_len);
        if (n > 0) {
            buffer[n] = '\0';
            std::string msg(buffer);
            std::string ip = inet_ntoa(cli_addr.sin_addr);
            if (msg.substr(0, 10) == "LABKA_PING") {
                std::vector<std::string> parts;
                std::stringstream ss(msg);
                std::string token;
                while (std::getline(ss, token, '|')) parts.push_back(token);
                if (parts.size() >= 3) {
                    std::string name = parts[1];
                    std::string sender_ip = parts[2];
                    if (sender_ip == my_ip_) continue;
                    updatePeer(ip, name);
                }
            }
        }
    }
}

void LabkaCore::updatePeer(const std::string& ip, const std::string& name) {
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(peers_mtx_);
        auto it = peers_.find(ip);
        if (it == peers_.end()) { peers_[ip] = {ip, name, time(nullptr)}; changed = true; }
        else it->second.last_seen = time(nullptr);
    }
    if (changed && peer_cb_) {
        std::lock_guard<std::mutex> lock(peers_mtx_);
        std::vector<PeerInfo> vec;
        for (const auto& p : peers_) vec.push_back(p.second);
        peer_cb_(vec);
    }
}

std::vector<PeerInfo> LabkaCore::getPeers() const {
    std::lock_guard<std::mutex> lock(peers_mtx_);
    std::vector<PeerInfo> vec;
    for (const auto& p : peers_) vec.push_back(p.second);
    return vec;
}

std::shared_ptr<SSHClient> LabkaCore::getSSHClient(const std::string& ip) {
    std::lock_guard<std::mutex> lock(ssh_mtx_);
    if (ssh_clients_.find(ip) == ssh_clients_.end()) {
        ssh_clients_[ip] = std::make_shared<SSHClient>();
    }
    return ssh_clients_[ip];
}

void LabkaCore::addManualPeer(const std::string& ip, const std::string& name) {
    updatePeer(ip, name);
}