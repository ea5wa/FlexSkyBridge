# FlexSkyBridge

Plugin SoapySDR para Windows que conecta [SkyRoof]([https://www.skyroofproject.com/](https://ve3nea.github.io/SkyRoof/)) con el transceptor **FlexRadio 6600** para seguimiento satelital con corrección Doppler automática. Hecho con la ayuda de Claude Code.

<img width="1142" height="677" alt="screenshot" src="https://github.com/user-attachments/assets/14a70581-7b19-4978-a5a0-d79159751b89" />


## Características

- Habla directamente con el FlexRadio 6600 vía protocolo SmartSDR (TCP/4992) — sin necesidad de SmartSDR DAX ni smartsdr-iqtransfer
- Recibe IQ a **192.000 Hz** vía UDP directo (paquetes VITA-49), sin latencia de driver de audio
- Corrección Doppler en tiempo real usando CAT via rigctld integrado (puerto 4532)
- Mueve automáticamente el slice y el panadapter de SmartSDR al cambiar de frecuencia
- Compatible con AetherSDR u otros clientes SmartSDR funcionando simultáneamente

## Flujo de datos

```
SkyRoof → SoapySDR API → FlexSkyBridge.dll ←→ FlexRadio 6600
                                            ↕ TCP/4992  (protocolo SmartSDR)
                                            ↕ UDP/7891  (IQ VITA-49 @ 192 kHz)
              SkyRoof ← rigctld ← 127.0.0.1:4532  (corrección Doppler)
```

## Requisitos

- Windows 10/11 x64
- [PothosSDR](https://github.com/pothosware/PothosSDR/releases) (incluye SoapySDR)
- FlexRadio 6600 con SmartSDR v1.4 o superior
- SkyRoof 1.33 o superior 
- CMake 3.15+
- Visual Studio 18/2026

## Compilación

```powershell
git clone https://github.com/ea5wa/FlexSkyBridge.git
cd FlexSkyBridge

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

La DLL resultante queda en `build/Release/FlexSkyBridge.dll`.

## Instalación

Copia el DLL a la carpeta de módulos de SoapySDR:

```powershell
cmake --install build --config Release
```

O manualmente:

```powershell
copy build\Release\FlexSkyBridge.dll "C:\Program Files\PothosSDR\lib\SoapySDR\modules0.8\"
```

Verifica que SoapySDR lo detecta:

```powershell
SoapySDRUtil --probe="driver=flexskybridge"
```

## Configuración en SkyRoof

En SkyRoof, selecciona como SDR device:

```
driver=flexskybridge,radio=192.168.0.208,channel=1,udpport=7891,rigctld=4532
```

| Parámetro | Descripción | Valor por defecto |
|-----------|-------------|-------------------|
| `radio`   | IP del FlexRadio | `192.168.0.208` |
| `channel` | Canal DAX IQ (1-8) | `1` |
| `udpport` | Puerto UDP para recibir IQ | `7891` |
| `rigctld` | Puerto rigctld para Doppler | `4532` |

En SkyRoof, configura también:
- **CAT/Rig control**: rigctld en `127.0.0.1:4532`
- **Sample rate**: 192000 Hz
- **Format**: CF32

## Notas de uso

- No es necesario activar manualmente el DAX IQ 1 en SmartSDR DAX — el plugin crea su propio stream independiente
- El plugin puede coexistir con AetherSDR/SmartSDR-Win abierto simultáneamente
- El log de depuración se escribe en `C:\RADIO\FlexSkyBridge_debug.log`

## Arquitectura interna

| Fichero | Responsabilidad |
|---------|----------------|
| `FlexDevice.cpp` | Interfaz SoapySDR (setupStream, readStream, setFrequency, setSampleRate) |
| `SmartSDRClient.cpp` | Protocolo SmartSDR TCP: crea stream DAX IQ, envía slice tune para Doppler |
| `DaxIQReceiver.cpp` | Recepción UDP de paquetes VITA-49 y ring buffer CF32 |
| `RigCtldServer.cpp` | Servidor rigctld para recibir correcciones Doppler de SkyRoof |
| `Registration.cpp` | Registro del plugin en SoapySDR |

## Licencia

MIT — ver [LICENSE](LICENSE)

## Autor

EA5WA — [@ea5wa](https://github.com/ea5wa)
