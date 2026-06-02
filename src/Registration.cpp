#include <SoapySDR/Registry.hpp>
#include <SoapySDR/Device.hpp>
#include <fstream>
#include "FlexDevice.hpp"

static void dbgReg(const std::string& msg) {
    std::ofstream log("C:\\RADIO\\FlexSkyBridge_debug.log", std::ios::app);
    log << msg << "\n";
    log.close();
}

static std::vector<SoapySDR::Kwargs> findFlex(const SoapySDR::Kwargs& args) {
    dbgReg("=== findFlex llamado ===");

    std::vector<SoapySDR::Kwargs> results;

    if (args.count("radio")) {
        SoapySDR::Kwargs dev;
        dev["driver"]  = "FlexSkyBridge";
        dev["label"]   = "Flex 6600 via FlexSkyBridge (" + args.at("radio") + ")";
        dev["radio"]   = args.at("radio");
        dev["channel"] = args.count("channel") ? args.at("channel") : "1";
        dev["udpport"] = args.count("udpport") ? args.at("udpport") : "7891";
        results.push_back(dev);
        return results;
    }

    SoapySDR::Kwargs dev;
    dev["driver"]  = "FlexSkyBridge";
    dev["label"]   = "Flex 6600 via FlexSkyBridge";
    dev["radio"]   = "192.168.0.208";
    dev["channel"] = "1";
    dev["udpport"] = "7891";
    results.push_back(dev);

    return results;
}

static SoapySDR::Device* makeFlex(const SoapySDR::Kwargs& args) {
    return new FlexDevice(args);
}

static SoapySDR::Registry registerFlex(
    "FlexSkyBridge",
    &findFlex,
    &makeFlex,
    SOAPY_SDR_ABI_VERSION
);
