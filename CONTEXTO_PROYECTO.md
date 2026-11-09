# Pantalla EMT contexto técnico del proyecto

Revisión: **9 de noviembre de 2026**. Versión: **0.13.3**. Las correcciones se han implementado y probado localmente por fases; la instalación USB, conservación de configuración, apertura automática del portal y conexión/web están comprobadas; la transferencia OTA física y estabilidad prolongada siguen pendientes. La revisión WiFi y el diagnóstico de reinicios se describen en [WIFI_REINICIOS.md](validation/2026-11-09/WIFI_REINICIOS.md). Consultar el [informe de validación](validation/2026-11-09/REPORT.md) y el [plan](PLAN_CORRECCIONES.md).

## Orientación rápida

El proyecto activo es el de la **raíz**: firmware Arduino para ESP32, pantalla SSD1322 de 256 × 64, llegadas EMT Madrid, OpenWeather y configuración web servida desde el dispositivo. `departures-board/` conserva el proyecto británico de origen y no forma parte de esta compilación.

Leer primero `platformio.ini`, `include/globals.h`, `include/config_model.h`, `setup()` y `loop()` en `src/main.cpp`, y `setupWebServer()` en `src/web_server.cpp`. Los HTML sueltos son maquetas independientes, no la web desplegada.

## Archivos y responsabilidades

| Ruta | Responsabilidad |
| --- | --- |
| `platformio.ini` | Plataforma pioarduino **55.03.37** fijada, Arduino **3.3.7**, ESP32 a 240 MHz. Entornos `esp32dev` y `esp32dev_ota`. |
| `src/main.cpp` | Arranque, bucle, consultas EMT/OpenWeather, aplicación de configuración, dibujo, descanso y watchdog de tarea. |
| `src/web_server.cpp` | Única implementación de rutas HTTP y de la web activa. |
| `src/wifi_connection.cpp`, `include/wifi_recovery_policy.h` | Arranque sin bloqueo, reintentos con red guardada y portal automático tras 60 s sin conexión, además del acceso manual. |
| `src/boot_diagnostics.cpp`, `include/boot_trace.h` | Motivo de reset y última operación en SRAM RTC, sin escritura continua de flash. |
| `src/config.cpp` | Lectura y guardado verificado en LittleFS; diagnóstico de almacenamiento. |
| `src/secure_client.cpp` | Configuración TLS y bundle CA incluido en ESP-IDF. Requiere hora válida. |
| `src/ota_control.cpp`, `include/ota_control.h` | Cola de búsqueda/instalación manual, confirmación de versión y caducidad; ejecutada fuera del manejador HTTP. |
| `src/ota.cpp` | Consulta de release, descarga limitada, verificación SHA-256 e instalación OTA. |
| `include/globals.h` | Estado compartido y declaraciones de funciones de aplicación. |
| `include/config_model.h` | Valores iniciales, normalización y transacción de configuración. |
| `include/config_codec.h` | Codificación JSON compatible con configuraciones anteriores. |
| `include/runtime_policy.h` | Comparación temporal con desbordamiento, reintentos y control de consultas meteorológicas. |
| `include/board_model.h` | Agrupación única de filas, tiempos de páginas y elección del nombre visible. |
| `include/stop_detail.h` | Parser de detalle de parada EMT. |
| `include/ota_policy.h` | Versiones numéricas, selección de adjuntos, URLs permitidas y transferencia limitada. |
| `include/firmware_version.h` | Única definición de versión; OTA automática desactivada por defecto. |
| `lib/stationData/stationData.h` | Estructuras heredadas; capacidad de nueve servicios. |
| `custom_partitions.csv` | Flash de 4 MiB, dos aplicaciones de `0x1E0000` bytes y LittleFS de `0x20000`. |
| `data/` | Directorio vacío para crear una imagen LittleFS de inicialización explícita. No contiene credenciales. |
| `tests/`, `scripts/test_native.sh` | Pruebas C++ de comportamiento y de rutas web con sustitutos del hardware. |
| `scripts/validate_build.py` | Compila una copia temporal y guarda log y manifiesto SHA-256 de fuentes. |
| `validation/2026-11-09/` | Evidencias de cada fase, informe y binarios candidatos. |
| `stl/` | Carcasa superior e inferior v1.2. |
| `oled_simulator.html`, `web_preview_solarpunk.html` | Prototipos visuales no sincronizados automáticamente con C++. |
| `.pio/`, `build_output*.txt` | Productos de compilación y registros históricos; no son prueba del firmware instalado. |
| `departures-board/` | Fuentes, clientes de transporte británico, web y carcasa originales. |
| `source` | Archivo vacío heredado. |

## Hardware y arranque

La OLED usa SPI hardware, CS GPIO26, DC GPIO5 y sin pin de reset. El README de origen documenta CLK GPIO18, DIN GPIO23 y alimentación a 3,3 V. El ajuste físico depende del modelo de placa y pantalla.

`setup()` inicia serie a 115200 y OLED, monta LittleFS **sin formatear automáticamente**, carga configuración, conecta WiFi con WiFiManager y publica `pantalla-emt.local`. El punto de acceso de configuración se llama `Pantalla EMT`; se abre inmediatamente si no hay SSID configurado y, desde 0.13.3, después de 60 s sin conectar si hay red guardada. Antes de abrirlo se reintenta cada 30 s sin reiniciar. El botón web «Cambiar red WiFi» o BOOT/GPIO0 mantenido 3 s tras arrancar abre el portal manualmente. Usa `europe.pool.ntp.org` y zona horaria española con cambio estacional.

Sin parada entra en `ST_NEED_STOP`; con parada, en `ST_RUNNING`. El servidor escucha en el puerto 80. El watchdog de tarea se configura a 15 segundos y se alimenta nominalmente cada 250 ms; no se reinicia por el mero hecho de no recibir datos remotos. `/info` distingue datos, errores y mayor intervalo observado del bucle, e informa del motivo de reset, última operación y estado WiFi al arrancar.

## Red y datos

EMT se autentica con email y contraseña y conserva el token en RAM. `emtFetchArrivalsV1()` consulta llegadas nominalmente cada 30 segundos. El detalle de parada aporta `data[0].stops[0].name` y coordenadas; se consulta aunque no haya clave OpenWeather si se necesita nombre automático.

`stopRetry`, `authRetry` y `weatherRetry` aplican esperas de **60, 120, 300 y 600 segundos**, con máximo de 600. Las consultas meteorológicas correctas se repiten a los 10 minutos. Una modificación relevante de parada o credenciales restablece las esperas. Cambiar solo tiempos o brillo no provoca nuevas autenticaciones.

Las conexiones salientes usan validación TLS con el bundle CA del framework y necesitan hora válida; no existe fallback `setInsecure()`. Los clientes son síncronos: espaciar reintentos no garantiza que la web sea inmediata durante una consulta. Las pausas reales y la recuperación WiFi requieren medición en el ESP32.

## Configuración y presentación

`AppConfig` es `emt::Config<String>`. `/config.json` reside en el ESP32. Campos: credenciales, parada, nombre manual, clave meteorológica, tiempos, descanso, agrupación y brillo manual/automático. WiFiManager guarda WiFi por separado.

Valores iniciales: buses 45 s, meteorología 10 s, BUS2 desactivado, descanso desactivado (horas 0–6), agrupación activada, brillo manual 80, automático desactivado y niveles día/noche 128/20. Estos valores también se usan si falta el JSON. Las configuraciones anteriores conservan sus campos válidos y reciben valores iniciales para los nuevos.

Un nombre vacío significa automático. El nombre oficial se conserva en RAM, separado del manual; si falta se muestra «EMT Madrid». Un nombre manual antiguo, incluso «EMT Madrid», se respeta hasta vaciarlo en el formulario. No se escribe flash en cada consulta de nombre.

`applyConfig()` actualiza las variables de ejecución y reprograma la pantalla. El guardado escribe `/config.tmp`, verifica sus bytes y renombra sobre `/config.json`; solo después aplica la configuración y responde éxito. Un fallo conserva el estado activo. Un montaje fallido no borra datos: ver recuperación explícita en `README_KARIM.md`.

`buildRows()` alimenta tanto el conteo como el dibujo. Hay dos páginas de tres filas como máximo y nueve servicios recibidos; una fila puede agrupar dos llegadas. BUS2 exige más de tres filas y duración positiva. Cuando se omite, BUS utiliza todo el tiempo configurado. Cuando aparece, se reserva un mínimo de 5 segundos para la primera página.

El descanso suave mantiene la web y suspende consultas normales; el profundo apaga pantalla/WiFi y programa despertar. Requiere hora válida. Inicio y fin iguales siguen significando descanso todo el día. El brillo automático depende de los datos de amanecer y atardecer.

## Rutas y acceso

| Ruta | Comportamiento |
| --- | --- |
| GET `/`, `/config` | Formulario; nombre vacío para automático. |
| POST `/config` | Guardar, verificar y aplicar. Devuelve 500 si falla almacenamiento. El formulario completo incluye `fullForm=1`; peticiones parciales conservan casillas omitidas. |
| GET `/info` | Versión, OTA, almacenamiento, WiFi, datos, esperas y diagnóstico del bucle. |
| GET `/brightness?b=80` | Brillo temporal 1–255; no modifica la preferencia manual guardada. |
| POST `/wifi/setup` | Abre el portal manual sin borrar la red previa. Se cierra tras 3 min sin clientes si había credenciales. |
| GET `/reboot` | Reiniciar. Puede comprobar OTA si se instaló el perfil que la habilita. |
| GET `/sleepnow` | Solicitar descanso profundo cuando está habilitado. |

La web continúa en HTTP sin autenticación y las credenciales se guardan en texto en LittleFS. La protección de acceso local es trabajo separado; TLS saliente no la resuelve. Los valores interpolados en HTML se escapan.

## Compilación y OTA

Dependencias: WiFiManager 2.0.17, U8g2 2.36.5 y ArduinoJson 7.2.0. Ejecutar desde esta raíz:

```bash
bash scripts/test_native.sh
pio run -e esp32dev
```

`esp32dev` es el entorno predeterminado y **no instala OTA automáticamente**. `esp32dev_ota` habilita comprobación en la primera conexión con hora válida y parada configurada. La selección exige versión numérica superior, release estable, un único `firmware.bin` subido, tamaño compatible y digest SHA-256 válido. Descarga por HTTPS con redirecciones limitadas a dominios previstos de GitHub, comprueba tamaño y digest antes de activar la partición. Ver `release_readme.md`.

Para evitar modificar los productos `.pio` seguidos por Git, usar `scripts/validate_build.py`; durante esta revisión el core aislado está en `/tmp/pantalla-emt-validation/core`. Los directorios temporales pueden desaparecer: las evidencias persistentes están en `validation/`.

## Estado y cómo retomar

El punto de partida fue `master`, HEAD `84d4c27`, con modificaciones previas de nombre automático y varios productos `.pio` ya cambiados. Esos cambios de intención se integraron en la corrección; no se restauraron los archivos binarios existentes. No se ha creado commit ni publicado release.

Antes de continuar, consultar `git status --short` y el informe. La limpieza de los 871 archivos de `.pio` versionados quedó fuera del cambio funcional. No añadir todos los binarios al preparar un commit sin revisar su alcance.

Las suites nativas y las compilaciones no sustituyen las comprobaciones de OLED, flash real, TLS desde ESP32, instalación OTA, descanso/despertar o estabilidad durante 24 horas. Esos puntos siguen pendientes en el informe.

## Actualización manual por WiFi (0.13.2)

GET `/update` muestra estado y formularios; POST `/update/check` consulta y POST `/update/install` confirma la versión exacta. Ambos perfiles admiten esta operación. `processManualUpdates()` se ejecuta antes de la espera por parada y del descanso para atender solicitudes sin configurar EMT. La búsqueda no instala; la instalación reutiliza TLS, tamaño y SHA-256 del backend OTA. Ver [OTA_WIFI.md](validation/2026-11-09/OTA_WIFI.md) para resultados y límites.

## Acceso WiFi sin BOOT (0.13.3)

La placa del usuario solo tiene RST. `WifiRecovery` conserva credenciales y abre configuración tras 60 s sin conexión; `PortalWindow` permite 10 min en automático, 3 min en manual y extiende el plazo mientras hay clientes AP. Al cerrar se restaura la conexión de estación y se concede un nuevo minuto antes de reabrir. Sin WiFi no se ejecuta el descanso programado; el portal se dibuja con contraste legible. Este comportamiento sustituye la política de 0.13.1–0.13.2 de no abrir nunca un portal con SSID guardado. Ver [WIFI_SIN_BOOT.md](validation/2026-11-09/WIFI_SIN_BOOT.md).
