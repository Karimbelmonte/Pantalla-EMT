# Pantalla EMT 0.13.3

Permite configurar otra WiFi en placas que solo tienen RST, sin necesitar BOOT ni acceso previo a la web. Si la red guardada no conecta durante 60 segundos, la pantalla abre **Pantalla EMT**. Conecta el móvil a esa red y abre **http://192.168.4.1** para elegir una WiFi de 2,4 GHz y guardar su contraseña.

El portal automático permanece abierto diez minutos sin clientes y continúa abierto mientras haya un móvil conectado. Si termina sin configurar otra red, vuelve a intentar la guardada. Abrir el portal no borra las credenciales WiFi ni la configuración EMT; guardar otra red sustituye las credenciales WiFi. Sin conexión se pospone el descanso programado para mantener accesible esta recuperación.

El adjunto **firmware.bin** corresponde a `esp32dev`: instalación automática desactivada; siguientes actualizaciones mediante **Actualizar por WiFi → Buscar actualización → Descargar e instalar**. Desde 0.13.2, ese mismo menú permite instalar esta versión. El actualizador antiguo de 0.12 puede comprobar la release al reiniciar.

Validación: ocho suites nativas, compilación de ambos perfiles e instalación USB en ESP32. Se verificaron el hash grabado, la conservación de NVS/LittleFS, la apertura automática del punto de acceso y la web con WiFi conectada; el usuario confirmó la conexión. Sigue pendiente la prueba de transferencia OTA real y la estabilidad prolongada. Durante la validación de 0.13.2 se observó un OLED negro que se recuperó al desconectar completamente la alimentación; no se ha aislado su causa. Los avisos de arranque observados se detallan en el informe de validación.

SHA-256 de `firmware.bin`: `852e3422ff83eb524dc1d4afd8248060f413b36073645c50bda2623afcaac3a4`.
