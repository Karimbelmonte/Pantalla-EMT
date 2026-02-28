# Pantalla EMT – ESP32 Departure Board

Proyecto basado en **ESP32 + PlatformIO (Arduino framework)** que muestra en una pantalla OLED:

- 🚌 Llegadas EMT (MobilityLabs Basic API)
- 🌤️ Tiempo actual (OpenWeather)
- 🌐 Configuración vía web
- 💾 Persistencia en LittleFS
- 🔁 Watchdog hardware (Task WDT) + watchdog lógico (Soft WDT)

---

# 🧰 Requisitos

## Hardware

- ESP32 Dev Module
- Pantalla OLED compatible con U8g2
- Cable USB de datos (no solo carga)

## Software (Linux recomendado)

- Python 3.12+
- PlatformIO 6.x instalado con `pipx`
- Usuario en grupo `dialout`

---

# 🔧 Instalación de PlatformIO (recomendado con pipx)

```bash
sudo apt update
sudo apt install -y pipx
pipx ensurepath
source ~/.profile
pipx install platformio
```

Comprobar que se usa la versión correcta:

```bash
which pio
pio --version
```

Debe devolver algo como:

```
/home/usuario/.local/bin/pio
PlatformIO Core, version 6.x.x
```

---

# 📁 Estructura del proyecto

```
.
├── platformio.ini
├── src/
│   └── main.cpp
├── include/
│   └── stationData.h
└── README.md
```

---

# 🔄 Cómo actualizar el firmware (cada vez que cambias el código)

Cada vez que modifiques `src/main.cpp`:

## 1️⃣ Entrar en la carpeta del proyecto

```bash
cd ~/Escritorio/"3D prints"/"Pantalla EMT"
```

## 2️⃣ Compilar

```bash
pio run
```

Si todo está correcto verás:

```
[SUCCESS]
```

## 3️⃣ Subir al ESP32

Conecta el ESP32 por USB y ejecuta:

```bash
pio run -t upload
```

---

## Si se queda en `Connecting.....`

Procedimiento manual de bootloader:

1. Mantén pulsado **BOOT**
2. Pulsa y suelta **EN / RESET**
3. Suelta **BOOT** cuando empiece a escribir

---

# 🖥️ Monitor serie (logs)

Para ver el arranque y logs:

```bash
pio device monitor -b 115200
```

Salir del monitor:

```
Ctrl + ]
```

---

# 🌐 Configuración web

Una vez conectado a WiFi:

- Accede a `pantalla-emt.local`
- O a la IP local del ESP32
- Configura:
  - StopId
  - Nombre visible
  - Credenciales EMT
  - API Key OpenWeather
  - Brillo
  - Duración de pantallas

---

# 🛠️ Troubleshooting

---

## ❌ Error: Permission denied `/dev/ttyUSB0`

Añadir usuario al grupo dialout:

```bash
sudo usermod -aG dialout $USER
```

Cerrar sesión y volver a entrar  
(o temporalmente:)

```bash
newgrp dialout
```

Comprobar:

```bash
groups
```

Debe aparecer `dialout`.

---

## ❌ Error: Port is busy

Ver qué proceso usa el puerto:

```bash
sudo lsof /dev/ttyUSB0
```

Cerrar el proceso que aparezca.

Si es ModemManager:

```bash
sudo systemctl stop ModemManager
sudo systemctl disable ModemManager
```

---

## ❌ Error: resultcallback

Estás usando PlatformIO viejo (`/usr/bin/pio`).

Comprobar:

```bash
which pio
```

Debe apuntar a:

```
~/.local/bin/pio
```

Si no:

```bash
echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.bashrc
source ~/.bashrc
```

---

## ❌ Error: No serial data received

Posibles causas:

- No está en modo bootloader → usar botón BOOT
- Cable USB solo carga (no datos)
- Puerto incorrecto

Forzar puerto:

```bash
pio run -t upload --upload-port /dev/ttyUSB0
```

---

## ❌ No muestra buses

Comprobar en `/info`:

- StopId correcto
- Credenciales EMT correctas
- Token válido

---

## ❌ No muestra tiempo

Comprobar en `/info`:

- Weather API key configurada
- Coordenadas obtenidas
- Sin errores en Weather

---

# 🧹 Forzar borrado completo (si algo raro ocurre)

```bash
pio run -t erase
pio run -t upload
```

---

# 📦 Actualizar PlatformIO (opcional)

```bash
pipx upgrade platformio
```

---

# 🧠 Arquitectura básica

ESP32  
 ├── WiFiManager (solo WiFi)  
 ├── WebServer (/config, /info)  
 ├── EMT API (MobilityLabs Basic)  
 ├── OpenWeather API  
 ├── LittleFS (config.json)  
 ├── Task Watchdog (hardware)  
 └── Soft Watchdog (lógico)

---

# 🔁 Flujo normal de desarrollo

1. Editar `main.cpp`
2. `pio run`
3. `pio run -t upload`
4. `pio device monitor`

---

Proyecto listo para desarrollo iterativo seguro.
