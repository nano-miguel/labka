// src/core/labka_core.h
#ifndef LABKA_CORE_H
#define LABKA_CORE_H

#include <string>
#include <vector>
#include <map>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>
#include <memory>
#include "ssh_client.h"

struct PeerInfo {
    std::string ip;
    std::string device_name;
    time_t last_seen;
};

class LabkaCore {
  public:
    using PeerUpdateCallback = std::function<void(const std::vector<PeerInfo>&)>;

    LabkaCore();
    ~LabkaCore();

    void start(int tcp_port);
    void stop();
    void setPeerUpdateCallback(PeerUpdateCallback cb) { peer_cb_ = std::move(cb); }
    
    std::vector<PeerInfo> getPeers() const;
    std::shared_ptr<SSHClient> getSSHClient(const std::string& ip);
    void addManualPeer(const std::string& ip, const std::string& name);
    
    std::string getMyIP() const { return my_ip_; }
    std::string getMyHostname() const { return my_hostname_; }

  private:
    void radarBroadcastLoop();
    void radarListenLoop();
    void updatePeer(const std::string& ip, const std::string& name);

    std::string my_ip_;
    std::string my_hostname_;
    int udp_sock_ = -1;
    int tcp_port_;
    std::atomic<bool> running_{false};
    std::thread bcast_thread_, listen_thread_;

    std::map<std::string, PeerInfo> peers_;
    mutable std::mutex peers_mtx_;
    PeerUpdateCallback peer_cb_;

    std::map<std::string, std::shared_ptr<SSHClient>> ssh_clients_;
    std::mutex ssh_mtx_;
};

#endif // LABKA_CORE_H