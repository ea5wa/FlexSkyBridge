#include "DaxIQReceiver.hpp"

#include <stdexcept>
#include <string>
#include <fstream>
#include <chrono>
#include <algorithm>
#include <cstring>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

static void dbgDax(const std::string& msg) {
    std::ofstream log("C:\\RADIO\\FlexSkyBridge_debug.log", std::ios::app);
    log << msg << "\n";
    log.close();
}

// ─────────────────────────────────────────────────────────────────────────────
DaxIQReceiver::DaxIQReceiver()
    : ring_(RING_CAPACITY * 2, 0.0f)
{}

DaxIQReceiver::~DaxIQReceiver() {
    stop();
}

// ─────────────────────────────────────────────────────────────────────────────
void DaxIQReceiver::start(uint16_t udpPort, const std::string&) {
    if (running_.load()) return;
    udpPort_       = udpPort;
    running_       = true;
    captureThread_ = std::thread(&DaxIQReceiver::captureLoop, this);
    dbgDax("DaxIQReceiver UDP: arrancando en puerto " + std::to_string(udpPort));
}

void DaxIQReceiver::stop() {
    if (!running_.exchange(false)) return;
    dataReady_.notify_all();
    if (captureThread_.joinable()) captureThread_.join();
    dbgDax("DaxIQReceiver UDP: detenido");
}

// ─────────────────────────────────────────────────────────────────────────────
// VITA-49 header para FlexRadio DAX IQ: siempre 7 palabras (28 bytes)
//   Word 0: VRT header (big-endian)
//   Word 1: Stream ID
//   Word 2-3: Class ID
//   Word 4: Integer timestamp
//   Word 5-6: Fractional timestamp
// Payload: float32 little-endian interleaved I,Q (Windows ya es LE, sin swap)
// ─────────────────────────────────────────────────────────────────────────────
static constexpr int VITA49_HEADER_BYTES = 28;
static constexpr int MAX_UDP_PACKET      = 9000;

void DaxIQReceiver::captureLoop() {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        dbgDax("UDP: socket() falló err=" + std::to_string(WSAGetLastError()));
        return;
    }

    // Reusar dirección
    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse, sizeof(reuse));

    // Buffer de recepción 8 MB — evita descartes bajo carga
    int rcvbuf = 8 * 1024 * 1024;
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char*)&rcvbuf, sizeof(rcvbuf));

    // Timeout para que el bucle pueda comprobar running_
    DWORD tv = 200;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&tv, sizeof(tv));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(udpPort_);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock, (sockaddr*)&addr, sizeof(addr)) != 0) {
        dbgDax("UDP: bind() falló puerto=" + std::to_string(udpPort_) +
               " err=" + std::to_string(WSAGetLastError()));
        closesocket(sock);
        return;
    }

    dbgDax("UDP: bind OK en puerto " + std::to_string(udpPort_));

    std::vector<uint8_t> pkt(MAX_UDP_PACKET);
    bool firstPacket = true;

    while (running_.load()) {
        int n = recv(sock, (char*)pkt.data(), (int)pkt.size(), 0);
        if (n <= 0) continue;

        if (n < VITA49_HEADER_BYTES + 8) continue;  // mínimo 1 par IQ

        // Extraer tamaño total del paquete del header VITA-49 (big-endian)
        uint32_t hdrWord0 = ntohl(*(uint32_t*)pkt.data());
        int packetSizeWords = (int)(hdrWord0 & 0xFFFF);
        int hasTrailer      = (int)((hdrWord0 >> 26) & 1);

        int payloadWords = packetSizeWords - 7 - hasTrailer;
        if (payloadWords <= 0) continue;

        int payloadBytes = payloadWords * 4;
        if (VITA49_HEADER_BYTES + payloadBytes > n) continue;

        if (firstPacket) {
            dbgDax("UDP: primer paquete recibido n=" + std::to_string(n) +
                   " packetWords=" + std::to_string(packetSizeWords) +
                   " payloadBytes=" + std::to_string(payloadBytes));
            firstPacket = false;
        }

        const float* samples   = reinterpret_cast<const float*>(pkt.data() + VITA49_HEADER_BYTES);
        int          nFloats   = payloadBytes / sizeof(float);
        int          nPairs    = nFloats / 2;
        if (nPairs == 0) continue;

        {
            std::lock_guard<std::mutex> lock(ringMutex_);
            const size_t bufSize = ring_.size();  // RING_CAPACITY * 2

            for (int i = 0; i < nPairs; ++i) {
                // Descartar muestras más antiguas si el buffer está lleno
                if (count_.load() >= RING_CAPACITY) {
                    readPos_ = (readPos_ + 2) % bufSize;
                    count_.fetch_sub(1);
                }

                ring_[writePos_]     = samples[i * 2];
                ring_[writePos_ + 1] = samples[i * 2 + 1];
                writePos_ = (writePos_ + 2) % bufSize;
                count_.fetch_add(1);
            }
        }
        dataReady_.notify_one();
    }

    closesocket(sock);
    dbgDax("UDP: captureLoop terminado");
}

// ─────────────────────────────────────────────────────────────────────────────
// Espera hasta tener exactamente maxSamples (como SoapyFlexRadio original)
// ─────────────────────────────────────────────────────────────────────────────
int DaxIQReceiver::read(float* dest, int maxSamples, int timeoutMs) {
    std::unique_lock<std::mutex> lock(ringMutex_);

    bool ok = dataReady_.wait_for(lock,
        std::chrono::milliseconds(timeoutMs),
        [this, maxSamples] {
            return count_.load() >= maxSamples || !running_.load();
        });

    if (!ok || !running_.load()) return 0;
    if (count_.load() == 0)      return 0;

    int toRead = std::min(maxSamples, count_.load());
    const size_t bufSize = ring_.size();

    for (int i = 0; i < toRead; ++i) {
        dest[i * 2]     = ring_[readPos_];
        dest[i * 2 + 1] = ring_[readPos_ + 1];
        readPos_ = (readPos_ + 2) % bufSize;
    }
    count_.fetch_sub(toRead);
    return toRead;
}

int DaxIQReceiver::available() const {
    return count_.load();
}

void DaxIQReceiver::flush() {
    std::lock_guard<std::mutex> lock(ringMutex_);
    readPos_  = writePos_;
    count_    = 0;
}
