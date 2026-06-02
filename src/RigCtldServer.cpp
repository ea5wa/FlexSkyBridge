#include "RigCtldServer.hpp"

#include <sstream>
#include <fstream>
#include <cstring>
#include <chrono>

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>

static void dbgRig(const std::string& msg) {
    std::ofstream log("C:\\RADIO\\FlexSkyBridge_debug.log", std::ios::app);
    log << "[rigctld] " << msg << "\n";
}

void RigCtldServer::start(uint16_t port, FreqCallback onSetFreq) {
    if (running_.load()) return;

    port_       = port;
    onSetFreq_  = std::move(onSetFreq);
    running_    = true;

    listenThread_ = std::thread(&RigCtldServer::listenLoop, this);
    dbgRig("Servidor rigctld arrancado en puerto " + std::to_string(port_));
}

void RigCtldServer::stop() {
    if (!running_.exchange(false)) return;

    // Cerrar el socket de escucha para desbloquear accept()
    if (listenSock_ != (uintptr_t)-1) {
        closesocket((SOCKET)listenSock_);
        listenSock_ = (uintptr_t)-1;
    }
    if (listenThread_.joinable()) listenThread_.join();

    std::lock_guard<std::mutex> lk(clientMutex_);
    for (auto& t : clientThreads_)
        if (t.joinable()) t.detach();
    clientThreads_.clear();

    dbgRig("Servidor rigctld detenido");
}

void RigCtldServer::listenLoop() {
    SOCKET srv = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (srv == INVALID_SOCKET) {
        dbgRig("ERROR: socket() falló");
        return;
    }

    // Permitir reutilizar el puerto inmediatamente
    int opt = 1;
    ::setsockopt(srv, SOL_SOCKET, SO_REUSEADDR,
                 reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port_);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // solo localhost

    if (::bind(srv, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        dbgRig("ERROR: bind() falló en puerto " + std::to_string(port_));
        closesocket(srv);
        return;
    }
    if (::listen(srv, 4) != 0) {
        dbgRig("ERROR: listen() falló");
        closesocket(srv);
        return;
    }

    listenSock_ = (uintptr_t)srv;
    dbgRig("Escuchando en 127.0.0.1:" + std::to_string(port_));

    while (running_.load()) {
        SOCKET client = ::accept(srv, nullptr, nullptr);
        if (client == INVALID_SOCKET) break;

        dbgRig("Cliente conectado");
        std::lock_guard<std::mutex> lk(clientMutex_);
        clientThreads_.emplace_back([this, client]() {
            handleClient((uintptr_t)client);
        });
    }
    closesocket(srv);
}

// ─────────────────────────────────────────────────────────────────────────────
// Protocolo rigctld (Hamlib) — subconjunto mínimo para frecuencia
// ─────────────────────────────────────────────────────────────────────────────
void RigCtldServer::handleClient(uintptr_t clientSock) {
    SOCKET s = (SOCKET)clientSock;
    char   buf[1024];
    std::string buffer;

    // Timeout de recepción: 5 segundos
    DWORD tv = 5000;
    ::setsockopt(s, SOL_SOCKET, SO_RCVTIMEO,
                 reinterpret_cast<const char*>(&tv), sizeof(tv));

    static int cmdCount = 0;

    while (running_.load()) {
        int n = ::recv(s, buf, sizeof(buf) - 1, 0);
        if (n <= 0) break;
        buf[n] = '\0';
        buffer += buf;

        size_t pos;
        while ((pos = buffer.find('\n')) != std::string::npos) {
            std::string line = buffer.substr(0, pos);
            buffer.erase(0, pos + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;

            ++cmdCount;
            std::string response;

            // ── set_freq: "F 435804021" o "\set_freq 435804021" ─────────────
            if ((line.size() > 2 && line[0] == 'F' && line[1] == ' ') ||
                line.rfind("\\set_freq ", 0) == 0)
            {
                auto sp = line.find(' ');
                try {
                    double freq = std::stod(line.substr(sp + 1));
                    currentFreq_ = freq;
                    if (onSetFreq_) onSetFreq_(freq);
                    if (cmdCount <= 5 || cmdCount % 40 == 0)
                        dbgRig("set_freq #" + std::to_string(cmdCount) +
                               " = " + std::to_string(freq / 1e6) + " MHz");
                } catch (...) {
                    dbgRig("ERROR parseo freq: " + line);
                }
                response = "RPRT 0\n";
            }
            // ── get_freq: "f" o "\get_freq" ──────────────────────────────────
            else if (line == "f" || line == "\\get_freq") {
                std::ostringstream oss;
                oss << (long long)currentFreq_.load() << "\n" << "RPRT 0\n";
                response = oss.str();
            }
            // ── set_mode / get_mode — respuesta mínima ────────────────────────
            else if (line[0] == 'M' || line == "m" || line == "\\get_mode" ||
                     line.rfind("\\set_mode", 0) == 0) {
                if (line[0] == 'M')
                    response = "RPRT 0\n";
                else
                    response = "USB\n3000\nRPRT 0\n";
            }
            // ── dump_state — identificación mínima para que Hamlib acepte ────
            else if (line == "dump_state" || line.rfind("\\dump_state", 0) == 0) {
                response =
                    "0\n"          // protocol version
                    "1\n"          // rig model (dummy)
                    "2\n"          // ITU region
                    // RX freq ranges: min max modes low_power high_power vfo ant
                    "100000 6000000000 0x1ff -1 -1 0x10000003 0x3\n"
                    "0 0 0 0 0 0 0\n"
                    // TX freq ranges
                    "0 0 0 0 0 0 0\n"
                    "0 0 0 0 0 0 0\n"
                    // tuning steps
                    "0 0\n"
                    "0 0\n"
                    "RPRT 0\n";
            }
            // ── get_vfo / set_vfo ─────────────────────────────────────────────
            else if (line == "v" || line == "\\get_vfo") {
                response = "VFOA\nRPRT 0\n";
            }
            else if (line[0] == 'V' || line.rfind("\\set_vfo", 0) == 0) {
                response = "RPRT 0\n";
            }
            // ── PTT ───────────────────────────────────────────────────────────
            else if (line == "t" || line == "\\get_ptt") {
                response = "0\nRPRT 0\n";
            }
            else if (line[0] == 'T' || line.rfind("\\set_ptt", 0) == 0) {
                response = "RPRT 0\n";
            }
            // ── Comando "a" de SkyRoof: detecta si somos SkyCAT ────────────────
            // Responder RPRT -18 indica "no soy SkyCAT, usa rigctld estándar"
            // Esto hace que SkyRoof use F <freq> para enviar la frecuencia Doppler
            else if (line == "a") {
                response = "RPRT -18\n";
            }
            // ── Cualquier otro comando: ACK genérico ─────────────────────────
            else {
                response = "RPRT 0\n";
            }

            ::send(s, response.c_str(), (int)response.size(), 0);
        }
    }
    ::closesocket(s);
    dbgRig("Cliente desconectado");
}
