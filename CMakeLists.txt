cmake_minimum_required(VERSION 3.15)
project(FlexSkyBridge VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Localizar SoapySDR (PothosSDR) ───────────────────────────────────────────
list(APPEND CMAKE_PREFIX_PATH "C:/Program Files/PothosSDR")
find_package(SoapySDR REQUIRED)

# ── Fuentes del plugin ────────────────────────────────────────────────────────
set(SOURCES
    src/Registration.cpp
    src/FlexDevice.cpp
    src/SmartSDRClient.cpp
    src/DaxIQReceiver.cpp
    src/RigCtldServer.cpp
)

# ── DLL del plugin ────────────────────────────────────────────────────────────
add_library(FlexSkyBridge MODULE ${SOURCES})

target_include_directories(FlexSkyBridge PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${SoapySDR_INCLUDE_DIRS}
)

target_link_libraries(FlexSkyBridge PRIVATE SoapySDR)

# En Windows necesitamos Winsock2 para TCP/UDP
if(WIN32)
    target_link_libraries(FlexSkyBridge PRIVATE ws2_32 ole32 uuid)
endif()

# ── Instalar en la carpeta de módulos de SoapySDR ────────────────────────────
install(TARGETS FlexSkyBridge
    DESTINATION "C:/Program Files/PothosSDR/lib/SoapySDR/modules0.8"
)
