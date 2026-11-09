# Configuración WiFi sin BOOT — 0.13.3

Revisión local del 9 de noviembre de 2026. La pantalla del usuario solo dispone de RST y está fuera del alcance de su red guardada; 0.13.2 no ofrecía una vía automática de configuración en ese caso.

## Comportamiento

- Red ausente: portal inmediato, como antes. SSID guardado sin conexión: reintentos durante 60 s, después punto de acceso **Pantalla EMT**, con portal en **http://192.168.4.1**.
- Abrir el portal no borra WiFi ni configuración EMT. Guardar otra red sí sustituye explícitamente las credenciales WiFi; el portal de WiFiManager conserva su comportamiento de guardado incluso si la contraseña nueva es incorrecta.
- Portal automático: diez minutos de inactividad sin clientes. Portal manual web/BOOT, si la placa dispone de él: tres minutos. Un cliente conectado extiende el plazo; no se interrumpe a quien está configurando desde el móvil.
- Al cerrar sin nueva red, se vuelve a intentar la guardada y, si sigue fallando otro minuto, se abre configuración de nuevo. Mientras el portal está abierto se prioriza su acceso, con STA desactivada por WiFiManager si no estaba conectada; la recuperación de la red anterior se reanuda al cerrar.
- El error al leer SSID sigue siendo estado desconocido, no se interpreta como credenciales borradas. Las caídas breves no abren el portal ni reinician.
- Sin WiFi se pospone el descanso programado para mantener disponible esta recuperación. El portal usa contraste 128 y sale del modo de ahorro del OLED para que las instrucciones sean legibles; no se cambia el brillo guardado.
- El firmware imprime su versión al arrancar, además del motivo de reset y del estado de la WiFi. El portal registra apertura/cierre sin contraseñas.

## Validación

Las ocho suites nativas pasan (`no-boot-wifi-tests.log`). La política comprueba el umbral de 60 s, desconexiones breves, reconexión que reinicia el plazo, cierre y reapertura, ausencia/error de SSID, caducidad con y sin clientes y desbordamiento del reloj. La simulación de 24 h no es un ensayo físico de estabilidad.

Las comprobaciones de compilación e instalación se registran por separado. La publicación de esta versión como release 0.13.3 fue autorizada por el usuario.

## Resultado de compilación e instalación

Ambos perfiles compilan: `no-boot-wifi-build.log` y `no-boot-wifi-auto-build.log`, con manifiestos de fuentes y [artefactos](no-boot-wifi-artifacts.json). El perfil normal se instaló por USB a 115200 baudios en app0; el programador verificó el hash. La tabla de particiones, NVS, selección OTA y LittleFS se compararon byte a byte antes de arrancar y permanecieron idénticos.

El arranque informa **Firmware 0.13.3** y SSID guardado. Sin pulsar BOOT, el registro anunció apertura **automática** del portal; el ordenador detectó la red **Pantalla EMT** con señal 100. Se observó un solo arranque en 75 s. No se cambiaron credenciales mediante herramientas.

El registro incluyó avisos de watchdog ya inicializado y una llamada de alimentación sin tarea registrada, además de un reintento WiFi ya en curso. No se observó reinicio por esos avisos en esta ventana; no equivale a validación de estabilidad prolongada. La apertura del formulario en el móvil y el guardado de la nueva red se confirman por separado en [device-validation-0.13.3.json](device-validation-0.13.3.json).

## Seguimiento: conexión confirmada

La revisión posterior localizó la pantalla mediante mDNS en la red normal. La identidad se contrastó con la placa instalada y `/info` respondió con 0.13.3, WiFi conectada, configuración OK, aplicación ejecutándose y cero desconexiones durante esa observación. El usuario confirmó después «Ya está conectado». No se observó directamente el envío del formulario ni se solicitaron contraseñas por el chat. Una vez conectada, la web normal está en la IP asignada por el router o en `http://pantalla-emt.local/`; `192.168.4.1` corresponde solo al portal AP.
