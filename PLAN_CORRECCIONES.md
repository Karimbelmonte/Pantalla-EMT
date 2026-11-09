# Plan de correcciones de Pantalla EMT

Fecha: 9 de noviembre de 2026. Estado: **implementación local realizada por fases; validación física pendiente**. Resultados y evidencias en [REPORT.md](validation/2026-11-09/REPORT.md). Basado en el código local y en `CONTEXTO_PROYECTO.md`.

El objetivo es estabilizar las consultas de red, hacer fiable la configuración, corregir la presentación y asegurar el mecanismo OTA antes de distribuir una nueva versión. El trabajo se hará sobre el proyecto raíz, conservando los cambios locales existentes. Cada fase tendrá un cambio revisable y una validación propia.

## Fase 0 Preparar una referencia reproducible

- Registrar el estado de Git, los cambios locales y las versiones efectivamente instaladas de PlatformIO y la plataforma ESP32.
- Compilar el estado inicial en una copia de trabajo o con salida aislada para no confundir los productos generados con los archivos de `.pio` ya versionados.
- Conservar el resultado y los avisos relevantes como referencia. Si falla, separar ese fallo inicial de los que introduzcan las correcciones.
- Inicializar y comprobar la salida serie a 115200, evitando registrar contraseñas, claves o tokens.
- Preparar un modo de validación con instalación OTA automática desactivada: los reinicios de prueba no deben sustituir el firmware en evaluación por una release anterior.

**Criterio de cierre:** fuentes iniciales identificadas, resultado de compilación registrado y mecanismo de diagnóstico preparado. No atribuir a esta compilación validación del dispositivo.

## Fase 1 Controlar fallos de red y recuperación

Archivos principales: `src/main.cpp` e `include/globals.h`.

- Comprobar el vencimiento del temporizador antes de intentar obtener coordenadas o meteorología.
- Programar el siguiente intento en todas las salidas de error. Propuesta inicial de espera progresiva: 60, 120, 300 y 600 segundos, con máximo de 600; reiniciar la secuencia tras un éxito.
- Mantener el intervalo de 10 minutos tras una consulta meteorológica correcta y el nominal de 30 segundos para llegadas EMT. Aplicar la misma disciplina de espera a fallos de autenticación que puedan repetirse desde varios consumidores.
- Usar comparaciones de tiempo que toleren el desbordamiento de `millis()` y definir cuándo un cambio de parada o credenciales reinicia la espera.
- Revisar los límites de tiempo de HTTP y la duración acumulada de autenticación, coordenadas y meteorología frente al watchdog. Espaciar peticiones no elimina por sí solo el bloqueo de una llamada HTTP síncrona.
- Separar el latido del bucle de la fecha de la última recepción válida. Una caída de Internet debe provocar reintentos controlados y un estado visible, no una secuencia de reinicios.
- Mostrar en `/info` antigüedad de los datos, último error y próxima tentativa, sin exponer secretos.

**Pruebas:** respuestas correctas, fallo DNS, timeout, 401/403, JSON inválido, falta de coordenadas, desconexión y recuperación WiFi, y desbordamiento del contador temporal. Verificar la secuencia de intentos con reloj simulado y medir en dispositivo las pausas reales de web/OLED durante los fallos.

**Criterio de cierre:** ningún camino de error lanza consultas en cada iteración; la recuperación ocurre sin reinicio manual y se distingue la falta de datos recientes de un bloqueo del programa.

## Fase 2 Guardar y aplicar configuración de forma coherente

Archivos principales: `include/globals.h`, `src/main.cpp` y `src/web_server.cpp`.

- Centralizar los valores iniciales y utilizarlos también cuando falte el archivo, falten campos o el JSON sea inválido. Conservar compatibilidad con los JSON existentes.
- Añadir un campo persistente de brillo manual, independiente de los niveles automáticos de día y noche. El brillo efectivo automático no debe sobrescribir la preferencia manual.
- Centralizar la aplicación de configuración para actualizar conjuntamente duraciones, planificador, brillo y estados dependientes de parada o credenciales.
- Preparar y validar una configuración candidata antes de sustituir la activa. Guardar en archivo temporal y reemplazar el anterior solo tras comprobar escritura y lectura, verificando las garantías reales de LittleFS durante la implementación.
- Si el guardado falla, conservar la configuración activa y responder con error; el navegador debe mostrarlo y no anunciar éxito.
- Evitar el formateo automático silencioso de LittleFS como respuesta a un fallo de montaje. Mostrar el error y reservar la recuperación destructiva para una operación explícita.
- Mantener `/brightness` como ajuste temporal y hacer persistente el brillo guardado desde el formulario. Explicar esa diferencia en la interfaz si resulta necesario.

**Pruebas:** configuración antigua, archivo ausente o corrupto, valores fuera de rango, campos omitidos, escritura fallida y recarga tras reinicio. Comprobar que las duraciones cambian sin reiniciar y que un error de escritura no cambia el estado activo.

**Criterio de cierre:** lo que la web confirma como guardado está persistido, se aplica en ejecución y se recupera al reiniciar; los valores iniciales son consistentes.

## Fase 3 Completar el nombre automático y la paginación

Archivos principales: `src/main.cpp`, `src/web_server.cpp` y las estructuras compartidas que sean necesarias.

- Dar al nombre vacío el significado de modo automático. Mantener separado el nombre configurado del nombre resuelto para que una consulta no desactive accidentalmente ese modo.
- Conservar los nombres manuales existentes, incluido un nombre guardado como `EMT Madrid`; no se puede inferir si fue elegido por el usuario. Vaciarlo desde el formulario activará el modo automático.
- Verificar el campo de nombre oficial con documentación primaria o una respuesta real anonimizada de EMT antes de depender de su estructura. Si falta, mostrar un texto de reserva sin convertirlo en nombre manual persistente.
- Invalidar el nombre resuelto al cambiar de parada; evitar escrituras de flash en cada consulta.
- Calcular una sola representación de filas que compartan el conteo, las dos páginas y el dibujo. Mantener la capacidad actual de nueve servicios recibidos y dos páginas de tres filas; no añadir una tercera página dentro de esta corrección.
- Si no se muestra BUS2, dedicar a BUS todo el tiempo configurado. Si se muestra, repartir el total respetando un mínimo de 5 segundos para BUS; validar BUS2 frente a ese límite y normalizar configuraciones antiguas incompatibles.
- Reprogramar el ciclo coherentemente cuando cambien configuración o cantidad de filas. Mantener el comportamiento de agrupación de una o dos llegadas por fila.

**Pruebas:** nombre manual, vacío, oficial ausente, cambio de parada y reinicio; cero, tres, cuatro, seis y nueve filas resultantes; varias llegadas de una misma línea; agrupación activada/desactivada; BUS2 desactivado o con duración límite. Verificar tanto las filas visibles como el tiempo total de buses.

**Criterio de cierre:** el nombre automático funciona desde el formulario y los criterios para dibujar, contar y temporizar páginas coinciden.

## Fase 4 Corregir OTA antes de publicar

Archivos principales: `src/main.cpp`, definición compartida de versión y `release_readme.md`.

- Unificar la versión del firmware y de la web en una sola definición.
- Comparar numéricamente versiones válidas, aceptando el prefijo `v`; actualizar únicamente si la versión remota es superior. Rechazar formatos no reconocidos sin instalar nada.
- Seleccionar exactamente `firmware.bin`, validar metadatos y tamaño frente a la partición, y rechazar releases sin un candidato inequívoco.
- Sustituir `setInsecure()` por validación de certificados con un mecanismo compatible con la plataforma instalada. Verificar certificados, sincronización de hora y redirecciones de GitHub durante la implementación; no conservar un fallback inseguro.
- Extender la validación TLS a los clientes EMT y OpenWeather que comparten los helpers. Un fallo de certificado debe seguir la política de reintentos de la fase 1.
- Revisar la interacción entre descarga, watchdog y arranque. Si la comprobación o descarga falla, continuar con el firmware instalado cuando sea posible y registrar el motivo.
- Documentar el procedimiento de actualización y recuperación por USB. No dar por supuesto que la tabla de dos particiones habilita rollback automático: verificar el soporte real antes de prometerlo.

**Pruebas:** versión igual, inferior, superior y mal formada; varios adjuntos; ausencia de `firmware.bin`; tamaño inválido; fallo TLS, hora no válida, redirección y descarga interrumpida. Las pruebas automáticas deben comprobar selección y decisiones sin descargar ni instalar firmware real.

**Criterio de cierre:** no se instala una versión anterior ni un adjunto arbitrario, la identidad TLS se valida y existe un procedimiento de recuperación comprobable. La prueba real de instalación queda separada de la validación local.

## Fase 5 Consolidar y validar el conjunto

- Retirar los manejadores web obsoletos de `main.cpp`, preservando los helpers que siga usando el firmware. Mantener `setupWebServer()` como punto único de registro de rutas.
- Retirar declaraciones sin uso y actualizar `CONTEXTO_PROYECTO.md`, `README_KARIM.md` y `release_readme.md` con el comportamiento final.
- Fijar la plataforma de compilación a la versión que haya superado las pruebas. No mezclar una actualización de dependencias con estas correcciones.
- Revisar el diff de fuentes y documentación por separado de los productos generados. La limpieza de los 871 archivos de `.pio` seguidos por Git será un cambio independiente, para conservar la trazabilidad.

**Validación local:** pruebas de comportamiento para temporizadores, configuración, agrupación y selección OTA; compilación completa y revisión del tamaño de flash/RAM. Introducir solo las separaciones de código necesarias para esas pruebas.

**Validación en ESP32:** arranque, configuración conservada, consulta EMT y meteorología, cambios de parámetros, caída y recuperación WiFi, brillo automático, descanso suave/profundo y despertar. Hacer una prueba continua de al menos 24 horas para observar estabilidad, sin tomarla como garantía de funcionamiento indefinido. Mantener la instalación OTA automática desactivada hasta su prueba específica.

**Criterio de cierre:** resultados locales y físicos registrados por separado, fallos encontrados resueltos o explicitados y documentación consistente con la versión candidata. Publicación e instalación se tratarán como pasos posteriores a esta preparación.

## Trabajo separado de acceso a la web

Las rutas sin autenticación merecen una mejora propia si la red no es de confianza. Antes de implementarla, definir cómo se provisiona y recupera el acceso, qué rutas requieren protección y cómo se protege el transporte. Añadir una contraseña sobre HTTP no resuelve por sí solo la confidencialidad. Esta mejora no se considerará resuelta por las correcciones OTA/TLS, que afectan a conexiones salientes.

## Entregables y orden de ejecución

Orden: referencia inicial → red → configuración → nombre y paginación → OTA/TLS → consolidación y validación física. Las fases funcionales se pueden validar localmente antes de resolver OTA, pero no se publicará una nueva release como parte de esas pruebas.

Cada fase dejará el cambio de fuentes, las pruebas relevantes y su resultado, y las limitaciones pendientes de comprobación física. Las correcciones locales están implementadas. No se ha publicado ni instalado una versión; las pruebas de dispositivo y de 24 horas siguen pendientes.

## Seguimiento de reinicios y WiFi

La candidata 0.13.1 añade recuperación WiFi sin reinicio, portal explícito y diagnóstico del reset. Ver [WIFI_REINICIOS.md](validation/2026-11-09/WIFI_REINICIOS.md); la causa del fallo de la pantalla instalada sigue pendiente de evidencia física.

## Seguimiento: descarga por WiFi

La candidata 0.13.2 incorpora búsqueda y confirmación de instalación desde la web, también con OTA automática desactivada. Ver [OTA_WIFI.md](validation/2026-11-09/OTA_WIFI.md). La publicación y la prueba de transferencia real al ESP32 siguen separadas de las pruebas locales.

## Publicación autorizada de 0.13.2

Publicada el 9 de noviembre de 2026 como última release estable. Fuentes en `codex/release-0.13.2`, etiqueta `v0.13.2`; descarga anónima y SHA-256 verificados. Ver [registro](validation/2026-11-09/publication-0.13.2.json). Las pruebas físicas y la causa de los reinicios siguen pendientes.

## Recuperación WiFi en placa sin BOOT

El usuario confirma que la placa solo tiene RST y solicita configurar otra red sin BOOT ni acceso web previo. Se añade en 0.13.3 un portal automático tras 60 s sin conexión, conservando credenciales. Validación de política, compilación y comprobación USB/AP por separado: [WIFI_SIN_BOOT.md](validation/2026-11-09/WIFI_SIN_BOOT.md).
