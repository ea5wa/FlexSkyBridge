#pragma once
#include <string>
#include <thread>
#include <atomic>
#include <functional>
#include <vector>
#include <mutex>
#include <cstdint>

// Minimal Hamlib rigctld-compatible TCP server.
// SkyRoof (and any Hamlib client) connects here and sends frequency updates
// every ~250ms via the CAT Rx interface. We forward each F command directly
// to the Flex slice via the provided callback.
class RigCtldServer {
public:
    using FreqCallback = std::function<void(double freqHz)>;

    RigCtldServer() = default;
    ~RigCtldServer() { stop(); }

    void start(uint16_t port, FreqCallback onSetFreq);
    void stop();
    void setCurrentFreq(double freqHz) { currentFreq_ = freqHz; }

private:
    void listenLoop();
    void handleClient(uintptr_t clientSock);

    uint16_t       port_{ 4532 };
    uintptr_t      listenSock_{ (uintptr_t)-1 };
    std::atomic<bool>   running_{ false };
    std::thread         listenThread_;
    FreqCallback        onSetFreq_;
    std::atomic<double> currentFreq_{ 145e6 };

    std::vector<std::thread> clientThreads_;
    std::mutex               clientMutex_;
};
