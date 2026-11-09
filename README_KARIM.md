# Operación de Pantalla EMT

Firmware 0.13.2. Diagnóstico de reinicios en [WIFI_REINICIOS.md](validation/2026-11-09/WIFI_REINICIOS.md). Estructura en [CONTEXTO_PROYECTO.md](CONTEXTO_PROYECTO.md); pruebas realizadas y pendientes en [el informe](validation/2026-11-09/REPORT.md).

## Preparación y compilación

Usar la raíz de Pantalla EMT, no `departures-board/`:

```bash
cd '/home/karim/Escritorio/Multimedia/Pantalla EMT'
bash scripts/test_native.sh
pio run -e esp32dev
```

La compilación usa pioarduino 55.03.37 y Arduino 3.3.7. El perfil predeterminado `esp32dev` tiene OTA automática desactivada. Su imagen es `.pio/build/esp32dev/firmware.bin`. Las pruebas nativas requieren `g++` y ArduinoJson descargado en `.pio/libdeps/esp32dev/` por PlatformIO.

Para mantener intactos los productos `.pio` versionados, se puede compilar una copia temporal:

```bash
python3 scripts/validate_build.py local
```

El script guarda manifiesto de fuentes y log en `validation/2026-11-09/`. Admite `--core-dir` para un core PlatformIO alternativo y `--environment` para elegir perfil. La revisión utilizó `/tmp/pantalla-emt-validation/core` como copia aislada; esa ruta no es una dependencia permanente del proyecto.

## Instalación y conexión

Cuando corresponda instalar en el ESP32 conectado por USB:

```bash
pio run -e esp32dev -t upload
pio device monitor -b 115200
```

Si hay varios dispositivos, especificar el puerto mediante `--upload-port`. Si no conecta el bootloader, utilizar BOOT y EN/RESET según la placa. La subida al dispositivo y la prueba física no se realizaron en esta revisión.

WiFiManager crea el punto de acceso `Pantalla EMT` cuando no hay red configurada. Si hay red guardada pero no conecta, sigue reintentando sin reiniciar ni pedir de nuevo la contraseña. Para cambiar de red, usar «Cambiar red WiFi» en la web o mantener BOOT/GPIO0 durante 3 s después de arrancar la aplicación (si la placa dispone de ese botón). Después abrir `http://pantalla-emt.local/` o la IP asignada. Completar parada, credenciales EMT y, opcionalmente, clave OpenWeather.

## Actualizar por WiFi

Desde el móvil u ordenador conectado a la misma red, abrir `http://pantalla-emt.local/update` (o `http://IP-DE-LA-PANTALLA/update`). También hay un enlace **Actualizar por WiFi** en la configuración y en `/info`.

1. Pulsar **Buscar actualización**. Esta acción solo consulta la última release estable de GitHub.
2. Si hay una versión superior válida, pulsar **Descargar e instalar** junto a esa versión. La confirmación caduca a los 10 minutos; después hay que buscar otra vez.
3. Mantener la alimentación mientras descarga. Durante la transferencia, la pantalla y la web pueden tardar en responder. Al terminar se reinicia y `/info` muestra la nueva versión. Si falla, consultar el estado y buscar otra vez para reintentar.

Funciona en ambos perfiles, incluido `esp32dev` con instalación automática desactivada; no exige tener configurada la parada. Necesita conexión a Internet y hora válida para HTTPS. Conserva NVS y LittleFS: solo escribe la partición de aplicación OTA.

Este menú existe desde 0.13.2. Para llegar desde una versión anterior sin USB, esa versión debe tener OTA operativa y debe publicarse la nueva release. La release preparada para ese salto es [v0.13.2](https://github.com/Karimbelmonte/Pantalla-EMT/releases/tag/v0.13.2). Ver [OTA_WIFI.md](validation/2026-11-09/OTA_WIFI.md).

## Configuración

- Nombre vacío: usa el nombre oficial de EMT cuando esté disponible; mientras tanto muestra «EMT Madrid».
- Buses: tiempo total entre sus páginas. BUS2 solo aparece con más de tres filas; dejar al menos 5 s para la primera página.
- Guardar aplica las duraciones sin reiniciar. El mensaje de éxito aparece después de verificar el archivo.
- Brillo manual guardado: persiste al reiniciar. El automático usa los niveles día/noche y datos meteorológicos. `/brightness?b=80` solo hace un ajuste temporal.
- Descanso suave: pantalla atenuada y web disponible. Descanso profundo: WiFi y pantalla apagados hasta despertar. Inicio y fin iguales equivalen a descanso permanente por horario.

## Diagnóstico

`/info` muestra motivo de reset, última operación registrada antes del reinicio, estado WiFi al arrancar, versión, perfil OTA, estado del almacenamiento, WiFi, errores, antigüedad de datos, esperas entre intentos y mayor intervalo del bucle. Sin hora válida, las consultas TLS se posponen y vuelven a intentarse; comprobar NTP y conectividad. Una espera tras un error no significa necesariamente que el firmware esté bloqueado.

Las credenciales EMT y la clave OpenWeather deben comprobarse por separado. Un error meteorológico no demuestra un fallo de la pantalla. Los intervalos de reintento crecen hasta 10 minutos; modificar la parada o las credenciales correspondientes permite reintentarlo.

## Inicialización o recuperación explícita de LittleFS

El firmware no formatea automáticamente si falla el montaje. Esto protege una configuración existente, pero una flash nueva puede necesitar una imagen de archivos inicial.

**El siguiente procedimiento reemplaza el contenido de LittleFS y borra la configuración de aplicación guardada.** Usarlo únicamente para inicializar una placa vacía o cuando se haya decidido descartar el almacenamiento dañado. `data/` está vacío: no incluye contraseñas ni una configuración predeterminada personalizada.

```bash
pio run -e esp32dev -t buildfs
pio run -e esp32dev -t uploadfs
```

Después reiniciar y configurar de nuevo desde la web. No usar un borrado completo de flash como paso habitual de actualización. WiFiManager usa almacenamiento separado de LittleFS.
