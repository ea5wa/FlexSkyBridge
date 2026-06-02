#pragma once
#include <string>
#include <thread>
#include <atomic>
#include <functional>
#include <mutex>
#include <cstdint>

class SmartSDRClient {
public:
    using FreqCallback = std::function<void(double freqHz)>;
    using PortCallback = std::function<void(uint16_t port)>;

    SmartSDRClient();
    ~SmartSDRClient();

    void connect(const std::string& radioIP, uint16_t port = 4992);
    void disconnect();
    bool isConnected() const { return connected_.load(); }

    uint32_t clientHandle()  const { return clientHandle_; }
    uint32_t daxIQStreamId() const { return daxIQStreamId_.load(); }

    void setSliceFrequency(int sliceIdx, double freqHz);
    void startDaxIQStream(int channel, uint16_t udpPort, int sampleRate);
    void removeDaxIQStream(uint32_t streamID);

    void onFrequencyChanged(FreqCallback cb) { freqCallback_ = std::move(cb); }
    void onDaxIQPort(PortCallback cb)        { daxIQPortCallback_ = std::move(cb); }

private:
    void sendCommand(const std::string& cmd);
    void receiveLoop();
    void parseLine(const std::string& line);

    intptr_t              sock_{ -1 };
    std::atomic<bool>     connected_{ false };
    std::thread           rxThread_;
    std::mutex            sendMutex_;

    uint32_t              clientHandle_{ 0 };
    std::atomic<uint32_t> seqNum_{ 1 };
    std::atomic<uint32_t> daxIQStreamId_{ 0 };
    std::atomic<uint32_t> existingDaxIQStreamId_{ 0 };

    std::string           firstClientId_;
    std::string           firstPanId_;

    FreqCallback          freqCallback_;
    PortCallback          daxIQPortCallback_;

    std::string           radioIP_;
};
