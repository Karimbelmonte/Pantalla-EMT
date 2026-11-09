# Validación de las correcciones de Pantalla EMT

Fecha: **9 de noviembre de 2026**. Registro de la candidata **0.13.0**. La candidata posterior **0.13.1** y sus pruebas WiFi están en [WIFI_REINICIOS.md](WIFI_REINICIOS.md). Las fases de implementación se han validado localmente antes de avanzar; la validación física permanece pendiente. No se ha publicado una release ni escrito firmware en un dispositivo.

## Resultado por fase

| Fase | Implementación | Evidencia local | Estado físico |
| --- | --- | --- | --- |
| 0 Referencia | Snapshot de cambios previos, compilación aislada, serie y perfil sin OTA automática. | `initial-source.diff`, `initial-status.txt`, `baseline-build.log`, `phase0-build.log`. | Serie y arranque pendientes. |
| 1 Red | Reintentos acotados, control temporal con desbordamiento y diagnósticos. | `phase1-tests.log`, `phase1-build.log`; suite `test_runtime.cpp`. | DNS/TLS/WiFi y latencia real pendientes. |
| 2 Configuración | Valores iniciales comunes, persistencia verificada, errores HTTP y aplicación inmediata. | `phase2-tests.log`, `phase2-build.log`; suites de configuración y rutas reales. | Flash y cortes de alimentación pendientes. |
| 3 Presentación | Nombre automático desde detalle EMT, filas compartidas y tiempos de páginas. | `phase3-tests.log`, `phase3-build.log`; suite `test_board.cpp`. | Respuesta autenticada EMT y OLED pendientes. |
| 4 OTA | Versiones, adjunto exacto, tamaño, SHA-256, TLS y transferencia acotada. | `phase4-tests.log`, `phase4-build.log`, `phase4-ota-build.log`; suite `test_ota.cpp`. | Descarga e instalación real pendientes. |
| 5 Consolidación | Duplicados retirados, plataforma fijada, documentación y pruebas de regresión. | `phase5-tests.log`, compilaciones `final` y `final-ota`, manifiestos de fuentes. | Descanso, recuperación y ensayo de 24 h pendientes. |

Los primeros intentos encontraron una dependencia Python ausente en el core aislado, una comprobación incorrecta de tipo JSON y un parámetro HTML sin uso. Se corrigieron antes de dar por válidas las fases correspondientes. Los logs finales de cada fase representan las ejecuciones que pasaron.

## Alcance de las pruebas automáticas

Se ejecutan cinco programas C++ con `g++ -std=c++17 -Wall -Wextra -Werror -pedantic`, mediante `bash scripts/test_native.sh`:

- **Runtime:** plazos alrededor del desbordamiento de `millis()`, secuencia 60/120/300/600 s, límite de espera, recuperación y exclusión de consultas repetidas antes del plazo. Los errores de transporte se representan mediante callbacks fallidos, no mediante conexiones reales.
- **Configuración:** JSON antiguo, valores omitidos, límites, ida y vuelta de JSON, fallo de escritura, lectura/verificación y renombrado. La transacción conserva el estado previo ante esos fallos. Los fallos de almacenamiento se inyectan en un sustituto; no son ensayos de corte eléctrico en LittleFS.
- **Presentación:** cero, tres, cuatro, seis y nueve filas; agrupación; tiempo total de buses; nombre manual, automático y de reserva; parser de detalle EMT y metadatos incompletos.
- **OTA:** versiones iguales, anteriores, superiores y mal formadas; adjunto único con nombre exacto; tamaño, digest y URLs; transferencia completa, truncada, bloqueada, demasiado lenta y con error de escritura. La selección y transferencia usan el código compartido con el firmware. No se instala una imagen durante estas pruebas.
- **Web:** compila e invoca `src/web_server.cpp` con sustitutos de WebServer, WiFi, OLED y persistencia. Comprueba 500 sin cambio de configuración, actualizaciones parciales/completas, casillas, nombre vacío, brillo guardado frente al temporal y escape HTML de valores con caracteres especiales.

Los manifiestos `*-sources.json` permiten vincular cada compilación a sus fuentes. No representan la versión que pueda estar instalada actualmente en una pantalla.

## Entorno de compilación

PlatformIO Core **6.1.19**, plataforma pioarduino **55.03.37**, Arduino ESP32 **3.3.7**, ESP-IDF **5.5.2**. La plataforma se fijó a su [release oficial](https://github.com/pioarduino/platform-espressif32/releases/tag/55.03.37), manteniendo las dependencias WiFiManager 2.0.17, U8g2 2.36.5 y ArduinoJson 7.2.0.

El core y la copia de compilación se aislaron bajo `/tmp/pantalla-emt-validation/`. Los productos `.pio` existentes del proyecto no se sustituyeron. El core temporal no es una instalación requerida para usar el proyecto en otro equipo.

La imagen candidata predeterminada tiene OTA automática desactivada. El perfil `esp32dev_ota` también se compila para comprobar que su código enlaza, pero no se instala.

Las compilaciones finales terminaron sin avisos ni errores del compilador. También pasó la generación de la imagen LittleFS vacía (`filesystem-build.log`).

| Perfil final | Aplicación | RAM estática | OTA automática |
| --- | --- | --- | --- |
| `esp32dev` | 1 418 300 bytes (72,1 %) | 60 772 bytes (18,5 %) | Desactivada |
| `esp32dev_ota` | 1 427 332 bytes (72,6 %) | 60 772 bytes (18,5 %) | Activada |

Binarios: [candidata sin OTA automática](firmware/esp32dev/firmware.bin) y [perfil con OTA automática](firmware/esp32dev_ota/firmware.bin). Los tamaños, hashes y rutas de artefactos se registran en [artifacts.json](artifacts.json). Cada partición de aplicación dispone de **1 966 080 bytes**. El valor de RAM informado por PlatformIO mide ocupación estática, no el pico de heap o pila durante TLS.

## Decisiones de implementación

- No reiniciar simplemente porque falle Internet. El watchdog vigila la tarea y la antigüedad de los datos se muestra por separado.
- Conservar la configuración activa si falla el guardado. Usar archivo temporal, verificación de bytes y renombrado sin borrar previamente el destino. LittleFS documenta la atomicidad de operaciones como rename en su [proyecto oficial](https://github.com/littlefs-project/littlefs); la resistencia de esta placa concreta a cortes aún debe probarse.
- No formatear automáticamente cuando falle LittleFS. Se añade `data/` vacío y un procedimiento explícito de `buildfs`/`uploadfs` para inicialización o recuperación decidida por el operador.
- Conservar nombres manuales antiguos. Solo un nombre vacío activa el modo automático; el resultado de EMT permanece en RAM.
- Consultar `data[0].stops[0].name` del recurso de detalle. La [guía oficial de EMT](https://gitlab.com/mobilitylabsmadrid/helps_and_utilities/openapi_documents/-/blob/master/openapi_doc/api_data.json) incluye esa estructura y un ejemplo de Cibeles. El fixture de prueba es sintético y no implica una consulta autenticada en vivo.
- Mantener el máximo de dos páginas de tres filas y nueve servicios recibidos. No se amplía el número de páginas en esta corrección.
- Usar el [bundle CA de ESP-IDF 5.5.2](https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32/api-reference/protocols/esp_crt_bundle.html) en EMT, OpenWeather y GitHub. Se eliminó todo `setInsecure()` del proyecto activo.
- Exigir metadatos SHA-256 del adjunto OTA y verificar los bytes antes de activar la partición. Si GitHub no ofrece digest, la candidata se rechaza. Los campos se contrastaron con la [documentación de assets de GitHub](https://docs.github.com/en/rest/releases/assets?apiVersion=2022-11-28).
- Mantener la instalación OTA desactivada por defecto; el perfil explícito que la habilita sigue sin contar con rollback de aplicación validado.

## Pruebas físicas pendientes

`serial-devices.json` contiene el resultado vacío de `pio device list --json-output`: este entorno no expone un puerto serie al que asociar inequívocamente la pantalla. Esto no determina si existe un ESP32 conectado fuera del entorno.

| Comprobación | Resultado que debe registrarse |
| --- | --- |
| Arranque por USB y OLED | Versión visible, salida serie, montaje sin pérdida de configuración y pantalla legible. |
| Configuración | Cambiar tiempos/brillo sin reiniciar; reiniciar y comprobar persistencia; probar archivo inexistente/corrupto y recuperación en una placa de prueba. |
| EMT y OpenWeather | Credenciales válidas, nombre oficial/manual, coordenadas, llegadas y meteorología coherentes. |
| TLS y hora | Consultas con NTP disponible; fallo controlado sin hora válida; recuperación posterior. Verificar cadenas reales desde ESP32. |
| Caída de WiFi/API | Contar intentos, medir pausas web/OLED y mayor intervalo del bucle; recuperar conectividad sin bucle de reinicios. |
| Paginación | Comprobar filas y duración total con BUS2 presente y omitido; agrupación activada y desactivada. |
| Descanso y brillo | Amanecer/atardecer, modo suave, modo profundo y despertar, incluida una transición de horario estacional simulada si procede. |
| OTA en placa de prueba | Imagen válida, descarga interrumpida, digest incorrecto, conservación del firmware anterior antes de completar y recuperación por USB. No asumir rollback automático después de arrancar una imagen nueva. |
| Ejecución de 24 horas | Registrar reinicios, evolución de memoria, errores de red y recuperación; sin atribuir estabilidad indefinida a un único ensayo. |

## Límites que permanecen

La web sigue usando HTTP sin autenticación; la protección del acceso local está separada del TLS saliente. Las llamadas de red siguen siendo síncronas y pueden pausar el bucle durante sus tiempos de espera. Los certificados están ligados al framework fijado y habrá que mantenerlos en futuras versiones. No se ha comprobado el ajuste físico de la carcasa ni el hardware.

No se han creado commits, publicado releases, enviado credenciales a servicios ni operado un dispositivo. Los cambios previos quedaron registrados y la intención del nombre automático se integró en la implementación final.
