# Actualización manual por WiFi — candidata 0.13.2

9 de noviembre de 2026. Solicitud del usuario: permitir descargar la actualización por WiFi. Implementación local; instalación física pendiente.

## Uso

En la web de la pantalla, abrir **Actualizar por WiFi**, también accesible en `/update`. Pulsar **Buscar actualización** y luego **Descargar e instalar** la versión que aparece. La búsqueda solo consulta; no modifica flash ni reinicia.

La descarga usa Internet desde el ESP32. Está disponible en `esp32dev` y `esp32dev_ota`, aunque el primer perfil tiene la instalación automática desactivada. Requiere WiFi e información horaria válida para HTTPS, pero no requiere parada ni credenciales EMT. En descanso profundo el dispositivo tiene que despertar antes de servir la web.

El firmware se escribe en la partición OTA de aplicación. NVS/WiFi y LittleFS/configuración no se escriben por este proceso. La pantalla se reinicia únicamente tras verificar y activar la imagen. Mientras se descarga, la pantalla y el servidor web pueden dejar de responder temporalmente porque la transferencia es síncrona.

## Implementación

- `src/ota_control.cpp`: separa búsqueda e instalación, mantiene la versión comprobada durante 10 minutos, rechaza versiones distintas en la confirmación y solicitudes duplicadas. Ante pérdida de conexión, caducidad o descarga fallida, se debe buscar otra vez.
- La petición HTTP devuelve 202 antes del trabajo de red. `loop()` espera al menos 500 ms y procesa la cola fuera del manejador HTTP, antes de la espera por parada o del descanso. El perfil automático sigue disponiendo de su comprobación e instalación al primer arranque con conexión utilizable.
- `src/web_server.cpp`: GET `/update` no tiene efectos de instalación; POST `/update/check` y `/update/install` usan token aleatorio de formulario por arranque. Las respuestas no se almacenan en caché. El token protege el envío del formulario frente a solicitudes sin token, pero no constituye autenticación de los usuarios de la red local.
- `src/ota.cpp`: ambos modos comparten TLS validado, selección de una versión superior estable, tamaño de partición, redirecciones HTTPS permitidas y verificación SHA-256 antes de activar el firmware. Se alimenta el watchdog entre redirecciones y durante la transferencia.
- No se ha añadido una entrada para URLs arbitrarias ni una imagen de filesystem al flujo de actualización.

## Validación

Las ocho suites nativas pasaron: `manual-ota-tests.log`. La nueva suite ejecuta el controlador real con backend de red/flash sustituido: búsqueda sin instalación, confirmación de versión exacta, cola y demora de respuesta, solicitudes duplicadas, caducidad, desbordamiento del reloj, pérdida de WiFi, fallo de descarga sin reinicio y éxito con reinicio. La suite web utiliza las rutas y el controlador reales para comprobar respuestas 403/409/503/202 y que ningún trabajo de descarga se realiza dentro de la respuesta HTTP.

Compilaciones: `manual-ota-build.log` y `manual-ota-auto-build.log`. Fuentes: `manual-ota-sources.json` y `manual-ota-auto-sources.json`. Artefactos: [manual-ota-artifacts.json](manual-ota-artifacts.json). Las imágenes 0.13.0 y 0.13.1 se conservan por separado.

No se ha probado aún una descarga real, la persistencia tras OTA, el corte físico de alimentación ni el arranque del nuevo firmware en la pantalla. No se garantiza rollback automático después de arrancar una aplicación defectuosa.

## Cómo llegar desde la pantalla anterior

La última release consultada mediante `gh release view` es [v0.12](https://github.com/Karimbelmonte/Pantalla-EMT/releases/tag/v0.12), con un adjunto `firmware.bin` de 1.325.744 bytes. No existe todavía una release publicada de esta candidata.

El código anterior de este repositorio consultaba `/releases/latest` al arrancar, con WiFi y parada configurada, e intentaba instalar cualquier versión distinta. Si esa es la versión que lleva la pantalla, una release nueva compatible podría recibirse sin USB. No se ha leído el binario instalado y no se puede garantizar ese salto sin probar el equipo.

Publicar una release estable como última versión puede hacer que otras pantallas con ese firmware anterior también la instalen al reiniciarse. La preparación local no publica ni activa ese cambio remoto. El artefacto propuesto para distribución es el perfil `esp32dev`: tras instalarlo, las siguientes actualizaciones se confirman manualmente desde la web.

## Prueba física pendiente

1. Comprobar en `/info` la versión instalada y la conexión del dispositivo.
2. Con una release nueva disponible, probar el salto desde el firmware anterior. Si no descarga, recoger el log antes de recurrir a USB.
3. En 0.13.2, comprobar que buscar no instala ni reinicia y que la confirmación muestra la versión prevista.
4. Probar error de consulta y corte de red durante descarga: mantener el firmware anterior activo y permitir un nuevo intento.
5. Completar una actualización y verificar versión, WiFi guardada, configuración EMT y funcionamiento de la pantalla.

## Binarios preparados

- [esp32dev — firmware.bin](firmware/0.13.2/esp32dev/firmware.bin): aplicación 1,436,976 bytes; RAM estática 61,708 bytes. SHA-256: `6582ac5835dfad9e9ff7a1d4b18b0a4f26033dd8015462945089a04b5f59f76b`.
- [esp32dev_ota — firmware.bin](firmware/0.13.2/esp32dev_ota/firmware.bin): aplicación 1,437,076 bytes; RAM estática 61,708 bytes. SHA-256: `7c01bda7eba782bc0f343ea91a60d03213c3535acd3cf8b3cec20b547bc7e8fb`.

Ambos perfiles compilan correctamente. Se comprobaron los hashes de fuentes frente a los manifiestos finales; no hubo avisos del compilador. El entorno offline emitió únicamente avisos sobre la omisión de la consulta de dependencias Python por falta de red.
