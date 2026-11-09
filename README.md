# Pantalla-EMT

Firmware para una pantalla OLED de llegadas de EMT Madrid y meteorología, basado en ESP32 y PlatformIO.

Consulta [CONTEXTO_PROYECTO.md](CONTEXTO_PROYECTO.md) para entender la estructura, localizar el código activo, compilar y conocer las particularidades del estado actual.

El [plan de correcciones](PLAN_CORRECCIONES.md) recoge las fases de trabajo, las pruebas y los criterios de aceptación.

Correcciones de **0.13.2**: [resultados y pruebas pendientes](validation/2026-11-09/REPORT.md). El perfil predeterminado compila con OTA automática desactivada.

La [revisión de WiFi y reinicios](validation/2026-11-09/WIFI_REINICIOS.md) añade reconexión sin reconfiguración automática y diagnóstico de la causa del reset.

La [actualización manual por WiFi](validation/2026-11-09/OTA_WIFI.md) permite buscar y confirmar una nueva versión desde `/update` en ambos perfiles.

[Descargar firmware 0.13.2](https://github.com/Karimbelmonte/Pantalla-EMT/releases/download/v0.13.2/firmware.bin). Las pruebas de hardware siguen pendientes; ver las notas de la release.
