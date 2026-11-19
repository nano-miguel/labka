#include "labka_core.h"

#include <iostream>
#include <sstream>
#include <cstring>
#include <chrono>
#include <algorithm>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <ifaddrs.h>
#include <netdb.h>

using namespace std::chrono;

// Cadencias del watchdog
static constexpr int TICK_MS          = 200;   // granularidad del sleep
static constexpr int CHECK_ONLINE_SEC = 5;     // keepalive cuando está online
static constexpr int BACKOFF_BASE_SEC = 5;     // 5,10,20,40,60...
static constexpr int BACKOFF_MAX_SEC  = 60;

LabkaCore::LabkaCore() {
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        my_hostname_ = std::string(hostname);
    } else {
        my_hostname_ = "Labka-PC";
    }

    struct ifaddrs *ifaddr, *ifa;
    if (getifaddrs(&ifaddr) == 0) {
        for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {

    if (ifa->ifa_addr == nullptr) continue;
    if (ifa->ifa_addr->sa_family == AF_INET) {
        char addr[NI_MAXHOST];

    if (getnameinfo(ifa->ifa_addr,
        sizeof(struct sockaddr_in),addr, NI_MAXHOST,
        nullptr, 0, NI_NUMERICHOST) == 0) {

    if (std::string(addr) != "127.0.0.1") {
            my_ip_ = std::string(addr);
        break;
                    }
                }
            }
        }
        freeifaddrs(ifaddr);
    }

    if (my_ip_.empty()) my_ip_ = "127.0.0.1";
}

LabkaCore::~LabkaCore() {
    stop();
}

void LabkaCore::start(int tcp_port) {
    if (running_) return;

    tcp_port_ = tcp_port;
    running_ = true;

    udp_sock_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_sock_ < 0) return;

    int broadcast = 1;
    setsockopt(udp_sock_, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));
    setsockopt(udp_sock_, SOL_SOCKET, SO_REUSEADDR, &broadcast, sizeof(broadcast));

    struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(2123);

    bind(udp_sock_, (struct sockaddr*)&addr, sizeof(addr));

    bcast_thread_    = std::thread(&LabkaCore::radarBroadcastLoop, this);
    listen_thread_   = std::thread(&LabkaCore::radarListenLoop, this);
    watchdog_thread_ = std::thread(&LabkaCore::reconnectLoop, this);   // ★
}

void LabkaCore::stop() {
    running_ = false;

    if (udp_sock_ >= 0) {
        shutdown(udp_sock_, SHUT_RDWR);
        close(udp_sock_);
        udp_sock_ = -1;
    }

    if (bcast_thread_.joinable())    bcast_thread_.join();
    if (listen_thread_.joinable())   listen_thread_.join();
    if (watchdog_thread_.joinable()) watchdog_thread_.join();
}

void LabkaCore::radarBroadcastLoop() {
    struct sockaddr_in bcast_addr;
        bcast_addr.sin_family = AF_INET;
        bcast_addr.sin_port = htons(2123);
        bcast_addr.sin_addr.s_addr = inet_addr("255.255.255.255");

    while (running_) {
        std::string payload = "LABKA_PING|" + my_hostname_ + "|" + my_ip_;
        sendto(udp_sock_, 
            payload.c_str(), 
            payload.size(), 0,
        (struct sockaddr*)&bcast_addr, sizeof(bcast_addr));

    for (int i = 0; i < 15 && running_; ++i) {
            std::this_thread::sleep_for(milliseconds(200));
        }
    }
}

void LabkaCore::radarListenLoop() {
    char buffer[1024];

    while (running_) {
        struct sockaddr_in cli_addr;
        socklen_t cli_len = sizeof(cli_addr);
        ssize_t n = recvfrom(udp_sock_, buffer, sizeof(buffer) - 1, 0,
    (struct sockaddr*)&cli_addr, &cli_len);

    if (n > 0) {
            buffer[n] = '\0';
            std::string msg(buffer);

    if (msg.substr(0, 10) == "LABKA_PING") {
        std::vector<std::string> parts;
        std::stringstream ss(msg);
        std::string token;

    while (std::getline(ss, token, '|')) parts.push_back(token);

    if (parts.size() >= 3) {
        std::string name = parts[1];
        std::string sender_ip = parts[2];
    if (sender_ip == my_ip_) 
    
    continue;updatePeer(sender_ip, name);
                }
            }
        }
    }
}

// ★ WATCHDOG: conecta al entrar, verifica y reintenta solo

void LabkaCore::reconnectLoop() {
    while (running_) {
        // 1) Snapshot de lo que toca revisar ahora (sin hacer red bajo el lock)
        std::vector<std::pair<std::string, 
        std::pair<std::string, 
        std::string>>> todo;{

        std::lock_guard<std::mutex> lk(links_mtx_);
        auto now = steady_clock::now();
    for (auto& [ip, link] : links_) {
    if (ip == my_ip_) 
        continue;
    if (link.user.empty()) 
        continue;
    if (!auto_reconnect_ && link.online) 
        continue;
    if (now >= link.next_try) {
        todo.push_back({ip, {link.user, link.pass}});
                }
            }
        }

        // 2) Verificar / reconectar
        for (const auto& item : todo) {
    if (!running_) break;

        const std::string ip = item.first;
        const std::string user = item.second.first;
        const std::string pass = item.second.second;

        auto client = getSSHClient(ip);
    if (!client) continue;
        bool ok = false;

    if (client->isConnected()) {
        ok = client->isAlive();          // keepalive
            }

    if (!ok) {
        client->disconnect();
        std::string err;
        ok = client->connect(ip, user, pass, err);}
            {
                
        std::lock_guard<std::mutex> lk(links_mtx_);
        auto it = links_.find(ip);
    if (it == links_.end()) continue;
        it->second.online = ok;

    if (ok) {
        it->second.attempts = 0;
        it->second.next_try = steady_clock::now() + seconds(CHECK_ONLINE_SEC);
    } else {
        it->second.attempts++;
        int shift = std::min(it->second.attempts - 1, 4);
        int wait  = std::min(BACKOFF_MAX_SEC, BACKOFF_BASE_SEC * (1 << shift));
        it->second.next_try = steady_clock::now() + seconds(wait);
                }
            }
        }

        // 4) Sleep granular para reaccionar a stop() rápido
        std::this_thread::sleep_for(milliseconds(TICK_MS));
    }
}

void LabkaCore::updatePeer(
    const std::string& ip, 
    const std::string& name) {
    bool changed = false;
    std::vector<PeerInfo> snapshot;

    {
        std::lock_guard<std::mutex> lock(peers_mtx_);

        auto it = peers_.find(ip);
    if (it == peers_.end()) {
            peers_[ip] = {ip, name, time(nullptr)};
            changed = true;
        for (const auto& p : peers_) snapshot.push_back(p.second);
    } else {
            it->second.last_seen = time(nullptr);
        }
    }

    if (changed && peer_cb_) peer_cb_(snapshot);
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

void LabkaCore::addManualPeer(
    const std::string& ip, 
    const std::string& name) {
    updatePeer(ip, name);
}

void LabkaCore::removePeer(const std::string& ip) {
    bool changed = false;
    std::vector<PeerInfo> snapshot;{

    std::lock_guard<std::mutex> lock(peers_mtx_);
    auto it = peers_.find(ip);
    if (it != peers_.end()) {
        peers_.erase(it);
        changed = true;
    for (const auto& p : peers_) snapshot.push_back(p.second);
        }
    }

    {
        std::lock_guard<std::mutex> lock(ssh_mtx_);
        auto it = ssh_clients_.find(ip);
    if (it != ssh_clients_.end()) {
            it->second->disconnect();
            ssh_clients_.erase(it);
        }
    }
    {
        std::lock_guard<std::mutex> lock(sessions_mtx_);
        sessions_.erase(ip);
    }
    {
        std::lock_guard<std::mutex> lock(links_mtx_);
        links_.erase(ip);
    }

    if (changed && peer_cb_) peer_cb_(snapshot);
}

void LabkaCore::markSessionActive(
    const std::string& ip, SessionKind kind) {
    std::lock_guard<std::mutex> lock(sessions_mtx_);
    SessionFlags& f = sessions_[ip];

    switch (kind) {
        case SessionKind::Terminal: f.terminal = true; break;
        case SessionKind::Files:    f.files    = true; break;
        case SessionKind::Vnc:      f.vnc      = true; break;
    }
}

bool LabkaCore::isDeviceOnline(const std::string& ip) const {
    std::lock_guard<std::mutex> lock(links_mtx_);
    auto it = links_.find(ip);
        return it != links_.end() && it->second.online;
}

bool LabkaCore::isDeviceActive(const std::string& ip) const {
    if (isDeviceOnline(ip)) 
        return true;

    {
        std::lock_guard<std::mutex> lock(sessions_mtx_);
        auto it = sessions_.find(ip);
    if (it != sessions_.end() &&
        (it->second.terminal || it->second.files || it->second.vnc)) {
        return true;
        }
    }

    std::lock_guard<std::mutex> lock(ssh_mtx_);
    auto it = ssh_clients_.find(ip);
    return it != ssh_clients_.end() && it->second && it->second->isConnected();
}

// ★ Guardar credenciales en memoria y pedir conexión inmediata
void LabkaCore::setPeerCredentials(
    const std::string& ip,                                  
    const std::string& user,                                  
    const std::string& pass) {

    std::lock_guard<std::mutex> lock(links_mtx_);

    PeerLink& l = links_[ip];
        l.user = user;
        l.pass = pass;
        l.attempts = 0;
        l.next_try = steady_clock::now();   // ya mismo
}

bool LabkaCore::hasPeerCredentials(
    const std::string& ip) const {
        std::lock_guard<std::mutex> lock(links_mtx_);
    auto it = links_.find(ip);
        return it != links_.end() && !it->second.user.empty();
}

int LabkaCore::reconnectAttempts(
    const std::string& ip) const {
        std::lock_guard<std::mutex> lock(links_mtx_);
    auto it = links_.find(ip);
        return it == links_.end() ? 0 : it->second.attempts;
}

void LabkaCore::requestReconnectAll() {
    std::lock_guard<std::mutex> lock(links_mtx_);
    auto now = steady_clock::now();
    for (auto& [ip, l] : links_) {
        l.attempts = 0;
        l.next_try = now;
    }
}

void LabkaCore::setAutoReconnect(bool enabled) {
    auto_reconnect_ = enabled;
    if (enabled) requestReconnectAll();
}