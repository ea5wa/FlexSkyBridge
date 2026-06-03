# FlexSkyBridge

Plugin SoapySDR para Windows que conecta [SkyRoof](https://ve3nea.github.io/SkyRoof/) con el transceptor **FlexRadio 6600** para seguimiento satelital con corrección Doppler automática y control de rotor. Hecho con la ayuda de Claude Code.

<img width="1142" height="677" alt="screenshot" src="https://github.com/user-attachments/assets/14a70581-7b19-4978-a5a0-d79159751b89" />

## Características

- Habla directamente con el FlexRadio 6600 vía protocolo SmartSDR (TCP/4992) — sin necesidad de SmartSDR DAX ni smartsdr-iqtransfer
- Recibe IQ a **192.000 Hz** vía UDP directo (paquetes VITA-49), sin latencia de driver de audio
- Corrección Doppler en tiempo real via rigctld integrado (puerto 4532)
- Control de rotor automático — lanza y cierra `rotctld` (hamlib) junto con el stream, enviando el tracking a PstRotator
- Mueve automáticamente el slice y el panadapter de SmartSDR al cambiar de frecuencia
- Compatible con AetherSDR u otros clientes SmartSDR funcionando simultáneamente

## Flujo de datos

```
                    ┌─────────────────────────────────────────┐
                    │           FlexSkyBridge.dll             │
SkyRoof ──SoapySDR──►  rigctld :4532   →  slice tune TCP/4992 ──► FlexRadio 6600
        ──rotctld──►  rotctld  :4533   →  PstRotator :4533        UDP VITA-49 ◄──┘
                    └─────────────────────────────────────────┘
```

- **TCP/4992** — protocolo SmartSDR: crea el stream DAX IQ, mueve el slice con Doppler
- **UDP/7891** — paquetes VITA-49 a 192 kHz directamente desde el radio
- **Puerto 4532** — rigctld embebido: recibe correcciones Doppler de SkyRoof
- **Puerto 4533** — rotctld (hamlib): recibe Az/El de SkyRoof y los reenvía a PstRotator

## Requisitos

- Windows 10/11 x64
- [PothosSDR](https://github.com/pothosware/PothosSDR/releases) (incluye SoapySDR)
- FlexRadio 6600 con SmartSDR v1.4 o superior
- [Hamlib](https://hamlib.github.io/) instalado en `C:\hamlib\` (para control de rotor)
- [PstRotator](http://www.qsl.net/yo3dmu/index_Page346.htm) configurado como servidor rotctld en puerto 4533
- SkyRoof 1.33 o superior (ver sección [Instalación con SkyRoof](#instalación-con-skyroof--ruta-correcta-del-módulo))
- CMake 3.15+ y Visual Studio 2019/2022

## Compilación

```powershell
git clone https://github.com/ea5wa/FlexSkyBridge.git
cd FlexSkyBridge

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

El DLL resultante queda en `build/Release/FlexSkyBridge.dll`.

## Instalación

SkyRoof **no usa** el directorio estándar de PothosSDR para cargar plugins SoapySDR. Usa su propia carpeta interna. Debes copiar la DLL en **dos sitios**:

### 1. Módulos de PothosSDR (para que SoapySDRUtil lo detecte)

```powershell
copy build\Release\FlexSkyBridge.dll "C:\Program Files\PothosSDR\lib\SoapySDR\modules0.8\"
```

### 2. Módulos internos de SkyRoof (imprescindible para que funcione)

La ruta depende de dónde esté instalado SkyRoof. Encuéntrala abriendo el log de SkyRoof (`%APPDATA%\Afreet\Products\SkyRoof\Logs\SkyRoof_YYYYMMDD.txt`) y buscando la línea:

```
Setting SoapySDR plugin path: C:\...
```

Copia la DLL a esa ruta:

```powershell
# Ajusta según lo que indique el log de SkyRoof
copy build\Release\FlexSkyBridge.dll "C:\RADIO\SkyRoof\SkyRoof\lib\SoapySDR\modules0.8\"
```

Rutas conocidas según versión de SkyRoof:

| Versión SkyRoof | Ruta del módulo |
|----------------|-----------------|
| 1.33 | `C:\RADIO\SkyRoof\lib\SoapySDR\modules0.8\` |
| 1.34 | `C:\RADIO\SkyRoof\SkyRoof\lib\SoapySDR\modules0.8\` |

### Verificación

Comprueba que SkyRoof carga el plugin correctamente en su log:

```
SDR started: Flex 6600 via FlexSkyBridge
[SOAPY_SDR_INFO]: [FlexSkyBridge] Stream activado
```

Si aparece `Device Flex 6600 via FlexSkyBridge is no longer available`, la DLL no está en la carpeta correcta de SkyRoof.

## Configuración en SkyRoof

En SkyRoof, selecciona como SDR device:

```
driver=flexskybridge,radio=192.168.0.208,channel=1,udpport=7891,rigctld=4532
```

| Parámetro | Descripción | Valor por defecto |
|-----------|-------------|-------------------|
| `radio` | IP del FlexRadio | `192.168.0.208` |
| `channel` | Canal DAX IQ (1-8) | `1` |
| `udpport` | Puerto UDP para recibir IQ | `7891` |
| `rigctld` | Puerto rigctld Doppler | `4532` |
| `rotctldexe` | Ruta a rotctld.exe | `C:\hamlib\bin\rotctld.exe` |
| `rotctldargs` | Argumentos de rotctld | `-m 3 -r 127.0.0.1:4533` |

En SkyRoof configura también:
- **CAT / Rig control** → rigctld en `127.0.0.1:4532`
- **Rotor** → rotctld en `127.0.0.1:4533`
- **Sample rate**: 192000 Hz · **Format**: CF32

## Control de rotor

FlexSkyBridge lanza automáticamente `rotctld.exe` al iniciar el stream y lo cierra al terminar. No es necesario arrancar rotctld manualmente.

El comando que ejecuta internamente es:

```
C:\hamlib\bin\rotctld.exe -m 3 -r 127.0.0.1:4533
```

PstRotator debe estar configurado en modo **rotctld server** escuchando en el puerto 4533.

Si necesitas apuntar a un PstRotator en otra máquina, pasa el parámetro en el device string:

```
...,rotctldargs=-m 3 -r 192.168.0.X:4533
```

## Notas de uso

- No es necesario activar el DAX IQ 1 en SmartSDR DAX — el plugin crea su propio stream independiente
- El plugin convive con AetherSDR / SmartSDR-Win abierto simultáneamente
- El log de depuración se escribe en `C:\RADIO\FlexSkyBridge_debug.log`


## Arquitectura interna

| Fichero | Responsabilidad |
|---------|----------------|
| `FlexDevice.cpp` | Interfaz SoapySDR · gestión del proceso rotctld |
| `SmartSDRClient.cpp` | Protocolo SmartSDR TCP: crea stream DAX IQ, slice tune Doppler |
| `DaxIQReceiver.cpp` | Recepción UDP de paquetes VITA-49 y ring buffer CF32 |
| `RigCtldServer.cpp` | Servidor rigctld embebido para correcciones Doppler |
| `Registration.cpp` | Registro del plugin en SoapySDR |

## Licencia

MIT — ver [LICENSE](LICENSE)

## Autor

EA5WA — [@ea5wa](https://github.com/ea5wa)
