# Actualización y publicación de Pantalla EMT

La versión de esta entrega es **0.13.3**. La publicación fue autorizada el 9 de noviembre de 2026; la instalación USB y la conexión/web están comprobadas, mientras que OTA física y estabilidad prolongada siguen pendientes. Consultar los resultados y pendientes en [el informe](validation/2026-11-09/REPORT.md).

## Perfiles de compilación

| Entorno | Instalación OTA al arrancar |
| --- | --- |
| `esp32dev` | Desactivada. Incluye búsqueda e instalación manual desde `/update`. |
| `esp32dev_ota` | Activada cuando hay WiFi y parada configurada. Usar después de validar el dispositivo. |

La versión se define una sola vez en `include/firmware_version.h`. El tag debe ser una versión numérica de dos o tres componentes, por ejemplo `v0.13.3`. Se aceptan versiones con o sin `v`; no se aceptan sufijos de prerelease.

## Preparar una candidata

1. Revisar el diff y conservar los cambios locales relevantes. `.pio` contiene muchos archivos versionados; no usar `git add .` sin revisar su contenido.
2. Ejecutar las suites nativas y compilar el perfil elegido con la plataforma fijada en `platformio.ini`.
3. Validar en hardware la configuración, red, presentación, descanso y recuperación. Mantener OTA automática desactivada durante estas pruebas.
4. Ambos perfiles reciben actualizaciones manuales por WiFi desde `/update`. Solo si se desea instalación automática al arrancar, compilar explícitamente `pio run -e esp32dev_ota`. Su binario se obtiene en `.pio/build/esp32dev_ota/firmware.bin`.
5. Revisar la versión candidata y sus resultados antes de publicar en `Karimbelmonte/Pantalla-EMT`.

La ruta manual separa búsqueda y confirmación: el usuario instala la versión mostrada, con validez de 10 minutos. Se usan POST y un token de formulario por arranque; esto evita solicitudes ajenas al formulario, pero no sustituye autenticación en una red no confiable.

## Contrato de la release

Publicar una release estable, no borrador ni prerelease, con un único adjunto llamado exactamente `firmware.bin`. Debe ser la imagen de aplicación ESP32 del perfil elegido, no el bootloader, tabla de particiones ni imagen combinada.

El firmware exige un adjunto en estado `uploaded`, tamaño positivo compatible con la partición y un digest `sha256:` de 64 dígitos hexadecimales en los metadatos de GitHub. Si faltan esos datos, rechaza la actualización. GitHub documenta tamaño, estado, URL y digest en sus [metadatos de assets](https://docs.github.com/en/rest/releases/assets?apiVersion=2022-11-28).

Solo una versión remota superior se instala; igualdad, versiones anteriores y formatos inválidos no producen actualización. El tamaño descargado debe coincidir con los metadatos y el digest debe coincidir antes de activar la nueva partición.

## Conexión y fallos

Las consultas y descargas validan certificados mediante el [bundle de ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32/api-reference/protocols/esp_crt_bundle.html). Necesitan hora válida y no recurren a TLS inseguro si falla la validación. El bundle se actualiza con el framework/firmware; no se actualiza por separado en ejecución.

La descarga admite hasta tres redirecciones HTTPS hacia las ubicaciones previstas de GitHub, tiene límite de inactividad de 10 s y total de transferencia de 180 s. Un fallo antes de completar y verificar la imagen aborta la actualización. La interrupción física de alimentación y el comportamiento de arranque de una imagen nueva requieren ensayo en la placa.

No se ha habilitado ni validado un mecanismo de rollback automático por fallo de la aplicación recién instalada. Las dos particiones OTA no bastan para garantizarlo. Conservar una imagen conocida y acceso USB como vía de recuperación; no borrar LittleFS durante una recuperación normal de firmware.

## Configuración sin BOOT: 0.13.3

Añade configuración WiFi sin BOOT después de 60 s sin conexión. El usuario autorizó publicar esta revisión como sucesora de 0.13.2. Ver [WIFI_SIN_BOOT.md](validation/2026-11-09/WIFI_SIN_BOOT.md).
