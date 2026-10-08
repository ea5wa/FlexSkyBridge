#include "SmartSDRClient.hpp"

#include <stdexcept>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <string>
#include <fstream>
#include <cstring>
#include <thread>
#include <chrono>

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>

namespace {
    struct WinsockInit {
        WinsockInit() {
            WSADATA wsa{};
            if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
                throw std::runtime_error("WSAStartup failed");
        }
        ~WinsockInit() { WSACleanup(); }
    };
    WinsockInit& winsockInstance() {
        static WinsockInit inst;
        return inst;
    }
}

static void dbgSdr(const std::string& msg) {
    std::ofstream log("C:\\RADIO\\FlexSkyBridge_debug.log", std::ios::app);
    log << msg << "\n";
    log.close();
}

SmartSDRClient::SmartSDRClient() {
    winsockInstance();
}

SmartSDRClient::~SmartSDRClient() {
    disconnect();
}

void SmartSDRClient::connect(const std::string& radioIP, uint16_t port) {
    if (connected_.load()) disconnect();

    radioIP_       = radioIP;
    firstClientId_ = "";
    firstPanId_    = "";
    clientHandle_  = 0;
    daxIQStreamId_ = 0;

    SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET)
        throw std::runtime_error("socket() failed");

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    if (inet_pton(AF_INET, radioIP.c_str(), &addr.sin_addr) != 1) {
        closesocket(s);
        throw std::runtime_error("IP invalida: " + radioIP);
    }

    if (::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        closesocket(s);
        throw std::runtime_error("No se pudo conectar a " + radioIP + ":4992");
    }

    sock_      = static_cast<intptr_t>(s);
    connected_ = true;
    rxThread_  = std::thread(&SmartSDRClient::receiveLoop, this);

    // Esperar handle (hasta 2 segundos)
    for (int i = 0; i < 200 && clientHandle_ == 0; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    sendCommand("sub slice all");
    sendCommand("sub radio all");
    sendCommand("sub daxiq all");
    sendCommand("sub display all");
    sendCommand("sub client all");
}

void SmartSDRClient::disconnect() {
    if (!connected_.exchange(false)) return;

    SOCKET s = static_cast<SOCKET>(sock_);
    shutdown(s, SD_BOTH);
    closesocket(s);
    sock_ = -1;

    if (rxThread_.joinable()) rxThread_.join();
}

// ─────────────────────────────────────────────────────────────────────────────
// Control de frecuencia — mueve el centro del panadapter
// SkyRoof llama esto con la frecuencia Doppler corregida
// ─────────────────────────────────────────────────────────────────────────────
void SmartSDRClient::setSliceFrequency(int sliceIdx, double freqHz) {
    double freqMHz = freqHz / 1e6;

    static int tuneCount = 0;
    ++tuneCount;

    // slice tune mueve el slice Y el pan, y funciona entre clientes
    std::ostringstream cmd;
    cmd << "slice tune " << sliceIdx
        << " " << std::fixed << std::setprecision(6) << freqMHz
        << " autopan=1";

    // Log completo solo las primeras 5 veces, luego cada 20
    if (tuneCount <= 5 || tuneCount % 20 == 0)
        dbgSdr("slice tune #" + std::to_string(tuneCount) +
               ": " + cmd.str());

    sendCommand(cmd.str());
}

// ─────────────────────────────────────────────────────────────────────────────
// Secuencia exacta de flexlib-go smartsdr-iqtransfer
// ─────────────────────────────────────────────────────────────────────────────
void SmartSDRClient::startDaxIQStream(int channel,
                                       uint16_t udpPort,
                                       int sampleRate)
{
    // Esperar hasta tener cliente Y panadapter (hasta 5 segundos)
    for (int i = 0; i < 500 && (firstClientId_.empty() || firstPanId_.empty()); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    dbgSdr("startDaxIQStream: clientId=" + firstClientId_ +
           " panId=" + firstPanId_ +
           " rate=" + std::to_string(sampleRate));

    if (firstClientId_.empty()) {
        dbgSdr("WARN: clientId no disponible, continuando sin bind");
    }
    if (firstPanId_.empty()) {
        dbgSdr("WARN: panId no disponible, el stream no tendrá panadapter");
    }

    // Esperar a que lleguen los status de streams preexistentes (sub daxiq all)
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // 1. Bind al primer cliente GUI
    if (!firstClientId_.empty())
        sendCommand("client bind client_id=" + firstClientId_);

    // 2. Obtener IP local del socket TCP (funciona con VPN/NAT)
    {
        sockaddr_in localAddr{};
        int addrLen = sizeof(localAddr);
        SOCKET s = static_cast<SOCKET>(sock_);
        if (getsockname(s, reinterpret_cast<sockaddr*>(&localAddr), &addrLen) == 0) {
            char ipBuf[INET_ADDRSTRLEN] = {};
            inet_ntop(AF_INET, &localAddr.sin_addr, ipBuf, sizeof(ipBuf));
            std::string localIP(ipBuf);
            dbgSdr("client ip enviado: " + localIP);
            sendCommand("client ip " + localIP);
        } else {
            dbgSdr("WARN: getsockname falló, omitiendo client ip");
        }
    }

    // 3. Registrar nuestro puerto UDP
    sendCommand("client udpport " + std::to_string(udpPort));

    // 3. Eliminar stream preexistente en este canal (evita estado "Busy")
    uint32_t prevId = existingDaxIQStreamId_.load();
    if (prevId != 0) {
        std::ostringstream prevHex;
        prevHex << std::hex << prevId;
        dbgSdr("Eliminando stream DAX IQ preexistente 0x" + prevHex.str());
        removeDaxIQStream(prevId);
        existingDaxIQStreamId_ = 0;
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }

    // Crear stream DAX IQ — sintaxis exacta de flexlib-go
    sendCommand("stream create type=dax_iq daxiq_channel=" +
                std::to_string(channel));

    // 4. Asignar al panadapter con sample rate
    if (!firstPanId_.empty()) {
        sendCommand("dax iq set " + std::to_string(channel) +
                    " pan=" + firstPanId_ +
                    " rate=" + std::to_string(sampleRate));
    }

    // 5. Esperar stream ID (hasta 1 segundo)
    for (int i = 0; i < 100 && daxIQStreamId_.load() == 0; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // 6. Ajustar rate del stream al valor solicitado
    if (daxIQStreamId_.load() != 0) {
        std::ostringstream cmd;
        cmd << "stream set 0x" << std::hex << daxIQStreamId_.load()
            << " daxiq_rate=" << std::dec << sampleRate;
        sendCommand(cmd.str());
        std::ostringstream sid;
        sid << std::hex << daxIQStreamId_.load();
        dbgSdr("stream set rate=" + std::to_string(sampleRate) +
               " streamId=0x" + sid.str());
    } else {
        dbgSdr("WARN: stream ID no capturado, no se pudo ajustar rate");
    }

    dbgSdr("startDaxIQStream completado");
}

void SmartSDRClient::removeDaxIQStream(uint32_t streamID) {
    if (streamID == 0) return;
    std::ostringstream cmd;
    cmd << "stream remove 0x" << std::hex << streamID;
    sendCommand(cmd.str());
}

void SmartSDRClient::sendCommand(const std::string& cmd) {
    if (!connected_.load()) return;

    uint32_t seq  = seqNum_.fetch_add(1);
    std::string line = "C" + std::to_string(seq) + "|" + cmd + "\n";

    std::lock_guard<std::mutex> lock(sendMutex_);
    SOCKET s = static_cast<SOCKET>(sock_);
    ::send(s, line.c_str(), static_cast<int>(line.size()), 0);
}

void SmartSDRClient::receiveLoop() {
    SOCKET s = static_cast<SOCKET>(sock_);
    std::string buffer;
    char chunk[4096];

    while (connected_.load()) {
        int n = ::recv(s, chunk, sizeof(chunk) - 1, 0);
        if (n <= 0) break;
        chunk[n] = '\0';
        buffer  += chunk;

        size_t pos;
        while ((pos = buffer.find('\n')) != std::string::npos) {
            std::string line = buffer.substr(0, pos);
            buffer.erase(0, pos + 1);
            if (!line.empty()) parseLine(line);
        }
    }
    connected_ = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Parser de mensajes SmartSDR
// ─────────────────────────────────────────────────────────────────────────────
void SmartSDRClient::parseLine(const std::string& line) {
    if (line.empty()) return;

    // Log de las primeras líneas
    static int lineCount = 0;
    if (lineCount++ < 50)
        dbgSdr("RADIO>> " + line);

    char type = line[0];

    // Handle: "H<hex>"
    if (type == 'H') {
        try {
            clientHandle_ = static_cast<uint32_t>(
                std::stoul(line.substr(1), nullptr, 16));
        } catch (...) {}
        return;
    }

    // Estado: "S<handle>|<payload>"
    if (type == 'S') {
        auto pipePos = line.find('|');
        if (pipePos == std::string::npos) return;
        std::string payload = line.substr(pipePos + 1);

        // ── Slice — capturar frecuencia Y pan ID ─────────────────────────────
        if (payload.rfind("slice ", 0) == 0) {
            // Frecuencia Doppler
            auto freqPos = payload.find("RF_frequency=");
            if (freqPos != std::string::npos) {
                try {
                    double freqMHz = std::stod(payload.substr(freqPos + 13));
                    if (freqCallback_) freqCallback_(freqMHz * 1e6);
                } catch (...) {}
            }

            // Pan ID — extraído del mensaje de slice (siempre disponible)
            if (firstPanId_.empty()) {
                auto panPos = payload.find(" pan=");
                if (panPos != std::string::npos) {
                    auto start = panPos + 5;
                    auto end   = payload.find(' ', start);
                    std::string panCandidate = payload.substr(start,
                        end == std::string::npos ? end : end - start);
                    // Solo aceptar si es un handle válido (0x40000000 etc)
                    if (panCandidate.rfind("0x", 0) == 0 && panCandidate != "0x0") {
                        firstPanId_ = panCandidate;
                        dbgSdr("firstPanId capturado desde slice: " + firstPanId_);
                    }
                }
            }
        }

        // ── Primer cliente GUI ────────────────────────────────────────────────
        if (payload.rfind("client ", 0) == 0 && firstClientId_.empty()) {
            auto cidPos = payload.find("client_id=");
            if (cidPos != std::string::npos) {
                auto start = cidPos + 10;
                auto end   = payload.find(' ', start);
                firstClientId_ = payload.substr(start,
                    end == std::string::npos ? end : end - start);
                dbgSdr("firstClientId capturado: " + firstClientId_);
            }
        }

        // ── Panadapter desde display pan ─────────────────────────────────────
        if (payload.rfind("display pan ", 0) == 0 && firstPanId_.empty()) {
            auto rest     = payload.substr(12);
            auto spacePos = rest.find(' ');
            std::string panCandidate = rest.substr(0, spacePos);
            if (panCandidate != "0x0" && !panCandidate.empty()) {
                firstPanId_ = panCandidate;
                dbgSdr("firstPanId capturado desde display pan: " + firstPanId_);
            }
        }

        // ── Stream DAX IQ ─────────────────────────────────────────────────────
        if (payload.find("type=dax_iq") != std::string::npos) {
            dbgSdr("STREAM DAX IQ: " + payload);

            // Extraer stream ID del payload ("stream 0xNNNNNNNN ...")
            uint32_t payloadStreamId = 0;
            if (payload.rfind("stream 0x", 0) == 0) {
                try {
                    auto idStart = 7u;
                    auto idEnd   = payload.find(' ', idStart);
                    std::string idStr = payload.substr(idStart,
                        idEnd == std::string::npos ? idEnd : idEnd - idStart);
                    payloadStreamId = std::stoul(idStr, nullptr, 16);
                } catch (...) {}
            }

            // Capturar stream preexistente al arrancar (solo si aún no tenemos uno)
            if (payloadStreamId != 0 && existingDaxIQStreamId_.load() == 0
                && daxIQStreamId_.load() == 0) {
                existingDaxIQStreamId_ = payloadStreamId;
                std::ostringstream ss; ss << std::hex << payloadStreamId;
                dbgSdr("Stream DAX IQ preexistente capturado: 0x" + ss.str());
            }

            // Detectar si nuestro stream activo pierde el pan (endpoint=Not Assigned)
            // y reasignarlo automáticamente
            if (payloadStreamId != 0
                && payloadStreamId == daxIQStreamId_.load()
                && payload.find("endpoint_type=Not Assigned") != std::string::npos
                && !firstPanId_.empty()) {
                dbgSdr("WARN: stream perdio pan, reasignando a " + firstPanId_);
                // Extraer el canal del payload
                auto chPos = payload.find("daxiq_channel=");
                int ch = 1;
                if (chPos != std::string::npos) {
                    try { ch = std::stoi(payload.substr(chPos + 14)); } catch (...) {}
                }
                sendCommand("dax iq set " + std::to_string(ch) +
                            " pan=" + firstPanId_);
            }
        }

        return;
    }

    // Respuesta: "R<seq>|<status>|<data>"
    if (type == 'R') {
        auto p1 = line.find('|');
        auto p2 = (p1 != std::string::npos) ? line.find('|', p1 + 1)
                                             : std::string::npos;
        if (p1 != std::string::npos && p2 != std::string::npos) {
            std::string status = line.substr(p1 + 1, p2 - p1 - 1);
            std::string data   = line.substr(p2 + 1);

            if (status == "0") {
                dbgSdr("Radio OK: " + line);

                // Capturar stream ID de la respuesta al stream create
                if (!data.empty() && data.size() <= 8 &&
                    data.find_first_not_of("0123456789abcdefABCDEF")
                        == std::string::npos) {
                    try {
                        uint32_t sid = std::stoul(data, nullptr, 16);
                        if (sid >= 0x20000000) {
                            daxIQStreamId_ = sid;
                            std::ostringstream ss;
                            ss << std::hex << sid;
                            dbgSdr("Stream ID capturado: 0x" + ss.str());
                        }
                    } catch (...) {}
                }
            } else {
                dbgSdr("Radio error: " + line);
            }
        }
        return;
    }
}