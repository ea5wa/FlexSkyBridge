#include "FlexDevice.hpp"
#include <SoapySDR/Logger.hpp>
#include <SoapySDR/Formats.hpp>
#include <SoapySDR/Types.hpp>
#include <stdexcept>
#include <string>
#include <iostream>
#include <fstream>
#include <sstream>

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>

static void dbg(const std::string& msg) {
    std::ofstream log("C:\\RADIO\\FlexSkyBridge_debug.log", std::ios::app);
    log << msg << "\n";
    log.close();
}

FlexDevice::FlexDevice(const SoapySDR::Kwargs& args) {
    dbg("=== Constructor llamado ===");
    radioIP_     = (args.count("radio")       ? args.at("radio")       : "192.168.0.208");
    daxChannel_  = (args.count("channel")     ? std::stoi(args.at("channel"))  : 1);
    udpPort_     = (args.count("udpport")     ? std::stoi(args.at("udpport"))  : 7891);
    rigctldPort_ = (args.count("rigctld")     ? std::stoi(args.at("rigctld"))  : 4532);
    if (args.count("rotctldexe"))  rotctldExe_  = args.at("rotctldexe");
    if (args.count("rotctldargs")) rotctldArgs_ = args.at("rotctldargs");
    dbg("radioIP=" + radioIP_ + " canal=" + std::to_string(daxChannel_) +
        " udpPort=" + std::to_string(udpPort_) +
        " rigctldPort=" + std::to_string(rigctldPort_) +
        " rotctldExe=" + rotctldExe_ +
        " rotctldArgs=" + rotctldArgs_);

    smartsdr_    = std::make_unique<SmartSDRClient>();
    daxReceiver_ = std::make_unique<DaxIQReceiver>();
    rigctld_     = std::make_unique<RigCtldServer>();

    smartsdr_->onFrequencyChanged([this](double hz) {
        currentFreqHz_ = hz;
        rigctld_->setCurrentFreq(hz);
    });
    dbg("Constructor completado OK");
}

FlexDevice::~FlexDevice() {
    dbg("=== Destructor llamado ===");
    rigctld_->stop();
    if (streaming_.load()) daxReceiver_->stop();
    if (smartsdr_->isConnected()) smartsdr_->disconnect();
    dbg("Destructor completado");
}

SoapySDR::Kwargs FlexDevice::getHardwareInfo() const {
    SoapySDR::Kwargs info;
    info["driver"]  = "FlexSkyBridge";
    info["hardware"]= "FLEX-6600";
    info["backend"] = "FlexSkyBridge-native";
    info["model"]   = "FLEX-6600";
    info["radio"]   = radioIP_;
    return info;
}

size_t FlexDevice::getNumChannels(const int dir) const {
    return (dir == SOAPY_SDR_RX) ? 1 : 0;
}

std::vector<std::string> FlexDevice::listAntennas(const int dir,
                                                   const size_t ch) const {
    return { "ANT1", "ANT2", "RX_A", "RX_B", "XVTA", "XVTB" };
}

std::string FlexDevice::getAntenna(const int dir, const size_t ch) const {
    return currentAntenna_;
}

// ── Sample rates — soportamos los rates que ofrece el DAX IQ de FlexRadio ────
std::vector<double> FlexDevice::listSampleRates(const int dir,
                                                 const size_t ch) const {
    return { 48000.0, 96000.0, 192000.0 };
}

SoapySDR::RangeList FlexDevice::getSampleRateRange(const int dir,
                                                    const size_t ch) const {
    return { SoapySDR::Range(48000.0, 192000.0) };
}

void FlexDevice::setSampleRate(const int dir, const size_t ch,
                                const double rate) {
    // Aceptar el rate que pide SkyRoof (48000, 96000 o 192000 Hz)
    // El radio y el dispositivo WASAPI se configuran en startDaxIQStream
    if (rate == 48000.0 || rate == 96000.0 || rate == 192000.0)
        currentSampleRate_ = rate;
    else
        currentSampleRate_ = 192000.0;  // default si pide algo fuera de rango
    dbg("setSampleRate=" + std::to_string(currentSampleRate_));
}

double FlexDevice::getSampleRate(const int dir, const size_t ch) const {
    return currentSampleRate_;  // siempre 48000
}

// ── Frecuencia ────────────────────────────────────────────────────────────────
void FlexDevice::setFrequency(const int dir, const size_t ch,
                               const std::string& name, const double freq,
                               const SoapySDR::Kwargs& args) {
    static int freqCallCount = 0;
    ++freqCallCount;
    dbg("setFrequency #" + std::to_string(freqCallCount) +
        " freq=" + std::to_string(freq / 1e6) + " MHz" +
        " connected=" + (smartsdr_->isConnected() ? "yes" : "NO"));

    currentFreqHz_ = freq;
    if (smartsdr_->isConnected())
        smartsdr_->setSliceFrequency(0, freq);
    else
        dbg("setFrequency: radio NO conectado, frecuencia descartada");
}

double FlexDevice::getFrequency(const int dir, const size_t ch,
                                 const std::string& name) const {
    return currentFreqHz_;
}

std::vector<std::string> FlexDevice::listFrequencies(const int dir,
                                                       const size_t ch) const {
    return { "RF" };
}

SoapySDR::RangeList FlexDevice::getFrequencyRange(const int dir,
                                                    const size_t ch,
                                                    const std::string& name) const {
    return { SoapySDR::Range(10000.0, 6000000000.0) };
}

// ── Ganancia ──────────────────────────────────────────────────────────────────
std::vector<std::string> FlexDevice::listGains(const int dir,
                                                const size_t ch) const {
    return { "RF" };
}


SoapySDR::Range FlexDevice::getGainRange(const int dir,
                                          const size_t ch) const {
    return SoapySDR::Range(0.0, 100.0, 1.0);
}

// ── Ancho de banda ────────────────────────────────────────────────────────────
double FlexDevice::getBandwidth(const int dir, const size_t ch) const {
    return getSampleRate(dir, ch);
}

std::vector<double> FlexDevice::listBandwidths(const int dir,
                                                const size_t ch) const {
    return { 48000.0, 96000.0, 192000.0 };
}

SoapySDR::RangeList FlexDevice::getBandwidthRange(const int dir,
                                                   const size_t ch) const {
    return { SoapySDR::Range(48000.0, 192000.0) };
}

// ── rotctld (proceso externo hamlib) ─────────────────────────────────────────
void FlexDevice::startRotctld() {
    if (rotctldExe_.empty()) return;

    // Matar instancia previa si quedara huérfana
    stopRotctld();

    std::string cmdLine = "\"" + rotctldExe_ + "\" " + rotctldArgs_;
    dbg("Arrancando rotctld: " + cmdLine);

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;   // sin ventana visible

    if (!CreateProcessA(nullptr,
                        const_cast<char*>(cmdLine.c_str()),
                        nullptr, nullptr, FALSE,
                        CREATE_NEW_PROCESS_GROUP,
                        nullptr, nullptr,
                        &si, &rotctldProc_)) {
        dbg("WARN: no se pudo arrancar rotctld (error " +
            std::to_string(GetLastError()) + ")");
        rotctldProc_ = { nullptr, nullptr, 0, 0 };
    } else {
        dbg("rotctld arrancado PID=" + std::to_string(rotctldProc_.dwProcessId));
    }
}

void FlexDevice::stopRotctld() {
    if (rotctldProc_.hProcess == nullptr) return;
    dbg("Deteniendo rotctld PID=" + std::to_string(rotctldProc_.dwProcessId));
    TerminateProcess(rotctldProc_.hProcess, 0);
    WaitForSingleObject(rotctldProc_.hProcess, 2000);
    CloseHandle(rotctldProc_.hProcess);
    CloseHandle(rotctldProc_.hThread);
    rotctldProc_ = { nullptr, nullptr, 0, 0 };
    dbg("rotctld detenido");
}

// ── Streaming ─────────────────────────────────────────────────────────────────
SoapySDR::Stream* FlexDevice::setupStream(const int dir,
                                           const std::string& format,
                                           const std::vector<size_t>& channels,
                                           const SoapySDR::Kwargs& args) {
    dbg("setupStream llamado, formato=" + format +
        " dir=" + std::to_string(dir));
    if (dir != SOAPY_SDR_RX) {
        dbg("ERROR: solo RX soportado");
        throw std::runtime_error("FlexSkyBridge: solo RX soportado");
    }
    if (format != SOAPY_SDR_CF32) {
        dbg("ERROR: formato no soportado: " + format);
        throw std::runtime_error("FlexSkyBridge: usa CF32, recibido: " + format);
    }
    dbg("setupStream OK");
    return reinterpret_cast<SoapySDR::Stream*>(this);
}

int FlexDevice::activateStream(SoapySDR::Stream* stream,
                                const int flags,
                                const long long timeNs,
                                const size_t numElems) {
    dbg("activateStream llamado");
    if (streaming_.exchange(true)) {
        dbg("activateStream: ya activo");
        return 0;
    }

    try {
        if (!smartsdr_->isConnected()) {
            dbg("Conectando a " + radioIP_);
            smartsdr_->connect(radioIP_);
            std::ostringstream hs;
            hs << std::hex << smartsdr_->clientHandle();
            dbg("Conectado OK. Handle=0x" + hs.str());
        }

        // Arrancar rotctld (hamlib) para control de rotor
        startRotctld();

        // Arrancar servidor rigctld para recibir frecuencias Doppler de SkyRoof
        dbg("Arrancando rigctld en puerto " + std::to_string(rigctldPort_));
        rigctld_->start(rigctldPort_, [this](double freqHz) {
            dbg("rigctld set_freq: " + std::to_string(freqHz / 1e6) + " MHz");
            currentFreqHz_ = freqHz;
            rigctld_->setCurrentFreq(freqHz);
            if (smartsdr_->isConnected())
                smartsdr_->setSliceFrequency(0, freqHz);
        });

        // Arrancar receptor UDP primero
        dbg("Arrancando DAX receiver en puerto " + std::to_string(udpPort_));
        daxReceiver_->start(udpPort_, radioIP_);
        dbg("DAX receiver OK en puerto " + std::to_string(udpPort_));

        // Secuencia completa de flexlib-go con rate correcto
        smartsdr_->startDaxIQStream(daxChannel_, udpPort_,
                                     static_cast<int>(currentSampleRate_));
        dbg("startDaxIQStream completado");

    } catch (const std::exception& e) {
        dbg("EXCEPCION en activateStream: " + std::string(e.what()));
        streaming_ = false;
        return SOAPY_SDR_STREAM_ERROR;
    }

    dbg("activateStream OK");
    SoapySDR::log(SOAPY_SDR_INFO, "[FlexSkyBridge] Stream activado");
    return 0;
}

int FlexDevice::deactivateStream(SoapySDR::Stream* stream,
                                  const int flags,
                                  const long long timeNs) {
    dbg("deactivateStream llamado");
    if (!streaming_.exchange(false)) return 0;
    stopRotctld();
    rigctld_->stop();
    daxReceiver_->stop();
    if (smartsdr_->isConnected()) smartsdr_->disconnect();
    dbg("deactivateStream OK");
    return 0;
}

void FlexDevice::closeStream(SoapySDR::Stream* stream) {
    dbg("closeStream llamado");
    deactivateStream(stream);
}

size_t FlexDevice::getStreamMTU(SoapySDR::Stream* stream) const {
    return 1024;
}

int FlexDevice::readStream(SoapySDR::Stream* stream,
                            void* const* buffs,
                            const size_t numElems,
                            int& flags,
                            long long& timeNs,
                            const long timeoutUs) {
    if (!streaming_.load()) return SOAPY_SDR_NOT_SUPPORTED;

    float* out       = static_cast<float*>(buffs[0]);
    int    timeoutMs = static_cast<int>(timeoutUs / 1000);
    if (timeoutMs < 1) timeoutMs = 1;

    static int rCount = 0;
    if (rCount++ < 5)
        dbg("readStream: pedidas=" + std::to_string(numElems) +
            " disponibles=" + std::to_string(daxReceiver_->available()));

    int read = daxReceiver_->read(out, static_cast<int>(numElems), timeoutMs);
    if (read == 0) return SOAPY_SDR_TIMEOUT;

    flags  = 0;
    timeNs = 0;
    return read;
}
