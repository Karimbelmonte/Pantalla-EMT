# Pantalla EMT 0.13.2

Añade actualización manual por WiFi desde la web: buscar una versión y confirmar su descarga e instalación. Incluye las correcciones de reconexión WiFi, diagnóstico de reinicios, configuración persistente, consultas EMT/OpenWeather y verificación TLS/SHA-256 preparadas en 0.13.0–0.13.1.

El `firmware.bin` propuesto corresponde al perfil `esp32dev`: la instalación automática al arrancar queda desactivada y las siguientes actualizaciones se confirman desde `/update`. La actualización modifica solo la aplicación y conserva NVS y LittleFS.

Validación local: ocho suites nativas y compilación de los perfiles manual y automático. Pendiente: instalación, transferencia OTA y validación física del ESP32; no se ha confirmado la causa de los reinicios reportados ni se garantiza rollback automático.

Nota de distribución: el firmware antiguo del repositorio puede instalar una release estable nueva al reiniciarse. Esta nota está preparada para revisión; no acredita que la release haya sido publicada.

SHA-256 del firmware propuesto: `6582ac5835dfad9e9ff7a1d4b18b0a4f26033dd8015462945089a04b5f59f76b`.
