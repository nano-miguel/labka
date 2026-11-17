#ifndef LABKA_CORE_H
#define LABKA_CORE_H

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <functional>
#include <atomic>
#include <ctime>
#include <chrono>

#include "ssh_client.h"

struct PeerInfo {
    std::string ip;
    std::string device_name;
    time_t last_seen;
};

enum class SessionKind {
    Terminal,
    Files,
    Vnc
};

class LabkaCore {
public:
    LabkaCore();
    ~LabkaCore();

    void start(int tcp_port);
    void stop();

    std::vector<PeerInfo> getPeers() const;

    std::shared_ptr<SSHClient> getSSHClient(const std::string& ip);

    void addManualPeer(const std::string& ip, const std::string& name);
    void removePeer(const std::string& ip);

    // ── Estado de enlace + sesiones ──
    void markSessionActive(const std::string& ip, SessionKind kind);
    bool isDeviceActive(const std::string& ip) const;
    bool isDeviceOnline(const std::string& ip) const;

    // ── Auto-reconexión ──
    // Credenciales SOLO en memoria (el baúl cifrado sigue en AuthManager)
    void setPeerCredentials(
        const std::string& ip,                     
        const std::string& user,                            
        const std::string& pass);

    bool hasPeerCredentials(const std::string& ip) const;
    int  reconnectAttempts(const std::string& ip) const;

    void requestReconnectAll();          // forzar reintento inmediato
    void setAutoReconnect(bool enabled); // on/off

    std::string getMyIP() const { return my_ip_; }
    std::string getMyHostname() const { return my_hostname_; }

    void setPeerCallback(std::function<void(const std::vector<PeerInfo>&)> cb) {
        peer_cb_ = cb;
    }

private:
    struct SessionFlags {
        bool terminal = false;
        bool files    = false;
        bool vnc      = false;
    };

    struct PeerLink {
        std::string user;
        std::string pass;
        bool online = false;
        int  attempts = 0;
        std::chrono::steady_clock::time_point next_try{};
    };

    void radarBroadcastLoop();
    void radarListenLoop();
    void reconnectLoop();                 // ★ watchdog
    void updatePeer(const std::string& ip, const std::string& name);

    std::string my_ip_;
    std::string my_hostname_;

    std::atomic<bool> running_{false};
    std::atomic<bool> auto_reconnect_{true};

    int tcp_port_ = 0;
    int udp_sock_ = -1;

    std::thread bcast_thread_;
    std::thread listen_thread_;
    std::thread watchdog_thread_;

    std::map<std::string, PeerInfo> peers_;
    mutable std::mutex peers_mtx_;

    std::map<std::string, std::shared_ptr<SSHClient>> ssh_clients_;
    mutable std::mutex ssh_mtx_;

    std::map<std::string, SessionFlags> sessions_;
    mutable std::mutex sessions_mtx_;

    std::map<std::string, PeerLink> links_;
    mutable std::mutex links_mtx_;

    std::function<void(const std::vector<PeerInfo>&)> peer_cb_;
};

#endif // LABKA_CORE_H