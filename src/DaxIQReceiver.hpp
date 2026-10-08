#pragma once
#include <cstdint>
#include <thread>
#include <atomic>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// DaxIQReceiver — recibe paquetes VITA-49 UDP directamente del radio FlexRadio
//
// El radio envía float32 LE interleaved I,Q en paquetes VITA-49 (cabecera 28
// bytes fija para streams DAX IQ). No se usa WASAPI ni el driver DAX de Windows.
// ─────────────────────────────────────────────────────────────────────────────

class DaxIQReceiver {
public:
    static constexpr int RING_CAPACITY = 262144;  // 256k muestras IQ

    DaxIQReceiver();
    ~DaxIQReceiver();

    // udpPort: puerto UDP donde el radio enviará los paquetes VITA-49
    // radioIP:  IP del radio — se usa para enviar un probe UDP que abre el camino de vuelta
    void start(uint16_t udpPort, const std::string& radioIP = "");
    void stop();

    bool isRunning() const { return running_.load(); }

    uint32_t sampleRate() const { return sampleRate_.load(); }

    // Espera hasta tener exactamente maxSamples muestras (como SoapyFlexRadio)
    int  read(float* dest, int maxSamples, int timeoutMs = 500);
    int  available() const;
    void flush();

private:
    void captureLoop();

    uint16_t                  udpPort_{ 7891 };
    std::string               radioIP_;
    std::atomic<bool>         running_{ false };
    std::thread               captureThread_;

    // Ring buffer CF32 (I,Q intercalados — 2 floats por muestra)
    std::vector<float>        ring_;
    size_t                    writePos_{ 0 };
    size_t                    readPos_{ 0 };
    std::atomic<int>          count_{ 0 };

    mutable std::mutex        ringMutex_;
    std::condition_variable   dataReady_;

    std::atomic<uint32_t>     sampleRate_{ 192000 };
};
