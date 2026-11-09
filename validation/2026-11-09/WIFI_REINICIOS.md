# Recuperación WiFi y diagnóstico de reinicios

Candidata local **0.13.1**, 9 de noviembre de 2026. Seguimiento de la versión candidata 0.13.0. El usuario informa de reinicios durante el funcionamiento normal y de tener que introducir WiFi otra vez; confirma que sigue usando una versión anterior. No se ha identificado aún la causa física o de ejecución de esos reinicios.

## Hallazgos y alcance

En el código anterior analizado, `autoConnect()` abría automáticamente el portal cuando fallaba la conexión a la red guardada. Por tanto, ver el portal no demostraba que se hubiese borrado la contraseña. Además, la aplicación reiniciaba si `autoConnect()` devolvía falso y después esperaba indefinidamente a estar conectada.

El watchdog y las operaciones de red síncronas, la actualización OTA anterior y posibles fallos de alimentación son vías distintas que requieren evidencia del dispositivo. No se atribuye el caso del usuario a ninguna de ellas sin una razón de reset o un log. La versión exacta y el binario instalados no se han leído desde la pantalla.

Las credenciales WiFi las gestiona el controlador en NVS, separado de `/config.json`. No se encontró una llamada de borrado de credenciales en el flujo normal de la aplicación. El framework sí contiene recuperación de NVS ante determinados errores de inicialización: el hecho de no borrar desde la aplicación no descarta problemas del almacenamiento o alimentación.

## Cambios de la candidata

- Arranque y recuperación WiFi no bloqueantes, en `src/wifi_connection.cpp`. Con SSID cargado se reintenta cada 30 s sin reiniciar ni abrir automáticamente el portal. Un error al leer el controlador se trata como desconocido, no como falta de credenciales.
- Portal automático solo si no hay red configurada. También se puede abrir mediante el botón web **Cambiar red WiFi** o manteniendo BOOT/GPIO0 durante 3 s **después de que haya arrancado la aplicación**, si la placa dispone de ese botón. Mantenerlo durante el reset puede entrar en el bootloader y no es el procedimiento de configuración.
- El portal manual con credenciales previas se cierra a los 3 min y permite volver a intentar la red. Abrirlo no borra el SSID ni la contraseña; guardar otra red es una modificación explícita del usuario.
- El servidor de aplicación y el de WiFiManager se alternan para no ocupar ambos el puerto 80. El portal se procesa desde el bucle, con un solo intento de conexión de hasta 5 s al guardar.
- Persistencia seleccionada también en el controlador activo después de abrir el portal. En Arduino 3.3.7, `WiFi.persistent()` cambia una bandera para la inicialización; no sustituye por sí solo a `esp_wifi_set_storage()` sobre un controlador ya iniciado. La [API oficial de Espressif](https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32/api-reference/network/esp_wifi.html) distingue almacenamiento RAM y FLASH.
- Se deshabilita la salida de depuración de WiFiManager para no imprimir contraseñas. El diagnóstico registra únicamente estados y códigos de desconexión.
- La reconexión vuelve a preparar NTP y mDNS conservando la zona horaria española. El perfil con OTA, si se usa, comprueba la release en la primera conexión con hora válida, incluso si arrancó sin red.

## Diagnóstico disponible

`src/boot_diagnostics.cpp` imprime al arrancar y expone en `/info`:

- Motivo de reset comunicado por el ESP32: software, watchdog, excepción/panic, brownout, encendido o despertar de descanso.
- Última operación registrada antes del reinicio: autenticación EMT, llegadas, detalle de parada, OpenWeather, OTA, web, conexión WiFi o descanso.
- Último tiempo de actividad registrado y si la aplicación había solicitado reinicio web, OTA o descanso.
- Presencia de SSID al arrancar, número de desconexiones y último código WiFi.

La traza se guarda en SRAM RTC con comprobación de integridad, sin escribir flash en cada consulta. Se descarta tras encendido o brownout y si no es válida. La última operación aporta contexto, no demuestra por sí sola la causa del fallo. Las categorías de reset están documentadas en el [ejemplo oficial de Espressif](https://github.com/espressif/arduino-esp32/blob/master/libraries/ESP32/examples/ResetReason/ResetReason2/ResetReason2.ino).

## Validación local

Se ejecutaron las siete suites nativas (`wifi-recovery-tests.log`). Además de las cinco suites previas:

- `test_wifi.cpp`: SSID guardado/ausente/error de lectura, caída de 24 h **simulada**, reintentos, reconexión, portal solicitado, pulsación larga y desbordamiento del reloj.
- `test_boot.cpp`: ejecuta el módulo real de diagnóstico con sustitutos de reloj y motivo de reset; comprueba captura de operación anterior, reinicio web y descarte de traza tras encendido/brownout.

La prueba de rutas reales comprueba también el POST `/wifi/setup` y los nuevos campos del diagnóstico. Las compilaciones y manifiestos están en `wifi-recovery-build.log`, `wifi-recovery-sources.json`, `wifi-recovery-ota-build.log` y `wifi-recovery-ota-sources.json`. Los binarios y hashes se relacionan en [wifi-recovery-artifacts.json](wifi-recovery-artifacts.json). Los manifiestos de ambas compilaciones se han contrastado con los archivos fuente finales.

- [Firmware 0.13.1 con OTA automática desactivada](firmware/0.13.1/esp32dev/firmware.bin): aplicación 1.421.984 bytes; RAM estática 61.604 bytes.
- [Firmware 0.13.1 con OTA automática activada](firmware/0.13.1/esp32dev_ota/firmware.bin): aplicación 1.431.056 bytes; RAM estática 61.604 bytes.

Ambos perfiles compilan correctamente y caben en la partición de aplicación de 1.966.080 bytes. Se conservan por separado los artefactos de la candidata 0.13.0.

La simulación no es un ensayo de 24 horas en el ESP32 ni valida que NVS conserve credenciales ante cortes físicos.

## Comprobación pendiente en la pantalla

1. Instalar por USB la candidata con OTA automática desactivada, conservando NVS y LittleFS: no ejecutar `erase` ni `uploadfs` para actualizar solo firmware.
2. Si había una red guardada válida, comprobar conexión sin volver a introducirla. Reiniciar la alimentación y repetir.
3. Interrumpir el router temporalmente y comprobar que la pantalla recupera la red sin entrar en configuración.
4. Si se reinicia sola, recoger `/info` o la línea `BOOT reason=...` del monitor serie a 115200. Un brownout orienta a alimentación; watchdog o panic requieren revisar el log y la operación registrada. Ninguno de esos resultados se presupone aquí.
5. Probar por separado el cambio manual de red y su persistencia tras desconectar la alimentación.

No hay un ESP32 serie accesible en el entorno de trabajo y no se ha instalado esta candidata ni publicado una release.
