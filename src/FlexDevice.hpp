#pragma once
#include <SoapySDR/Device.hpp>
#include <SoapySDR/Registry.hpp>
#include <memory>
#include <string>
#include <atomic>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "SmartSDRClient.hpp"
#include "DaxIQReceiver.hpp"
#include "RigCtldServer.hpp"

class FlexDevice : public SoapySDR::Device {
public:
    explicit FlexDevice(const SoapySDR::Kwargs& args);
    ~FlexDevice() override;

    // ── Identificación ────────────────────────────────────────────────────────
    std::string getDriverKey()   const override { return "FlexSkyBridge"; }
    std::string getHardwareKey() const override { return "FLEX-6600";     }
    SoapySDR::Kwargs getHardwareInfo() const override;

    // ── Canales ───────────────────────────────────────────────────────────────
    size_t getNumChannels(const int dir) const override;

    // ── Antenas ───────────────────────────────────────────────────────────────
    std::vector<std::string> listAntennas(const int dir,
                                          const size_t ch) const override;
    void        setAntenna(const int dir, const size_t ch,
                           const std::string& name) override {}
    std::string getAntenna(const int dir, const size_t ch) const override;

    // ── Sample rate ───────────────────────────────────────────────────────────
    void   setSampleRate(const int dir, const size_t ch,
                         const double rate) override;
    double getSampleRate(const int dir, const size_t ch) const override;
    std::vector<double> listSampleRates(const int dir,
                                        const size_t ch) const override;
    SoapySDR::RangeList getSampleRateRange(const int dir,
                                           const size_t ch) const override;

    // ── Frecuencia ────────────────────────────────────────────────────────────
    void   setFrequency(const int dir, const size_t ch,
                        const std::string& name, const double freq,
                        const SoapySDR::Kwargs& args) override;
    double getFrequency(const int dir, const size_t ch,
                        const std::string& name) const override;
    std::vector<std::string> listFrequencies(const int dir,
                                             const size_t ch) const override;
    SoapySDR::RangeList getFrequencyRange(const int dir,
                                          const size_t ch,
                                          const std::string& name) const override;

    // ── Ganancia ──────────────────────────────────────────────────────────────
    std::vector<std::string> listGains(const int dir,
                                       const size_t ch) const override;
    bool hasGainMode(const int dir, const size_t ch) const override { return false; }
    void setGainMode(const int dir, const size_t ch,
                     const bool automatic) override {}
    bool getGainMode(const int dir, const size_t ch) const override { return false; }
    void   setGain(const int dir, const size_t ch,
                   const double value) override {}
    double getGain(const int dir, const size_t ch) const override { return 0.0; }
    SoapySDR::Range getGainRange(const int dir,
                                 const size_t ch) const override;

    // ── Ancho de banda ────────────────────────────────────────────────────────
    void   setBandwidth(const int dir, const size_t ch,
                        const double bw) override {}
    double getBandwidth(const int dir, const size_t ch) const override;
    std::vector<double> listBandwidths(const int dir,
                                       const size_t ch) const override;
    SoapySDR::RangeList getBandwidthRange(const int dir,
                                          const size_t ch) const override;

    // ── Streaming IQ ──────────────────────────────────────────────────────────
    SoapySDR::Stream* setupStream(const int dir,
                                  const std::string& format,
                                  const std::vector<size_t>& channels,
                                  const SoapySDR::Kwargs& args) override;

    int activateStream(SoapySDR::Stream* stream,
                       const int flags = 0,
                       const long long timeNs = 0,
                       const size_t numElems = 0) override;

    int deactivateStream(SoapySDR::Stream* stream,
                         const int flags = 0,
                         const long long timeNs = 0) override;

    void   closeStream(SoapySDR::Stream* stream) override;
    size_t getStreamMTU(SoapySDR::Stream* stream) const override;

    int readStream(SoapySDR::Stream* stream,
                   void* const* buffs,
                   const size_t numElems,
                   int& flags,
                   long long& timeNs,
                   const long timeoutUs) override;
                   
    // ── DC Offset Mode ──────────────────────────────────────────────────────────
    bool hasDCOffsetMode(const int dir, const size_t ch) const override { return true; }
    void setDCOffsetMode(const int dir, const size_t ch,
                     const bool automatic) override {}
    bool getDCOffsetMode(const int dir, const size_t ch) const override { return true; }

private:
    void startRotctld();
    void stopRotctld();

    std::string radioIP_;
    int         daxChannel_{ 1 };
    uint16_t    udpPort_   { 7891 };

    double      currentFreqHz_    { 145e6 };
    double      currentSampleRate_{ 96000.0 };
    std::string currentAntenna_   { "ANT1" };

    std::unique_ptr<SmartSDRClient> smartsdr_;
    std::unique_ptr<DaxIQReceiver>  daxReceiver_;
    std::unique_ptr<RigCtldServer>  rigctld_;

    uint16_t          rigctldPort_{ 4532 };
    std::atomic<bool> streaming_{ false };

    // ── rotctld (control de rotor via hamlib) ─────────────────────────────────
    std::string        rotctldExe_ { "C:\\hamlib\\bin\\rotctld.exe" };
    std::string        rotctldArgs_{ "-m 3 -r 127.0.0.1:4533" };
    PROCESS_INFORMATION rotctldProc_{ nullptr, nullptr, 0, 0 };
};