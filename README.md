# Pantalla-EMT

Firmware para una pantalla OLED de llegadas de EMT Madrid y meteorología, basado en ESP32 y PlatformIO.

**Versión 0.13.3:** permite configurar otra WiFi sin BOOT. Si no consigue conectar a la red guardada durante un minuto, abre **Pantalla EMT**; conecta el móvil y visita **http://192.168.4.1**. La configuración EMT se conserva.

[Descargar firmware 0.13.3](https://github.com/Karimbelmonte/Pantalla-EMT/releases/download/v0.13.3/firmware.bin). Desde 0.13.2, usar **Actualizar por WiFi** en la web de la pantalla. El perfil predeterminado mantiene la instalación automática desactivada.

- [Guía de operación](README_KARIM.md).
- [Contexto técnico del proyecto](CONTEXTO_PROYECTO.md).
- [Plan de correcciones](PLAN_CORRECCIONES.md).
- [Configuración WiFi sin BOOT y validación 0.13.3](validation/2026-11-09/WIFI_SIN_BOOT.md).
- [Actualización manual por WiFi](validation/2026-11-09/OTA_WIFI.md).

Instalación USB, conservación de configuración, apertura automática del portal y conexión/web comprobadas. Transferencia OTA real y estabilidad prolongada pendientes; consultar las notas de la release.
