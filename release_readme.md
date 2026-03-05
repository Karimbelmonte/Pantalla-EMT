# 🚀 Cómo actualizar tu Pantalla-EMT vía GitHub (OTA)

¡Buenas noticias! Ya no necesitas conectar cables USB para actualizar tu dispositivo. Sigue estos sencillos pasos para subir y desplegar nuevas versiones de código.

## 1. Preparar el código (VS Code + PlatformIO)
Cuando realices cambios en el código (`main.cpp`, `web_server.cpp`, etc.):
1.  **Sube la versión:** Cambia el `#define VERSION_MINOR` en `src/main.cpp` (ej: de `10` a `11`).
2.  **Compila el proyecto:** Haz clic en el icono del "Check" (Build) en la barra inferior de PlatformIO.
3.  **Localiza el binario:** El archivo que necesitas está en:
    `Pantalla EMT/.pio/build/esp32dev/firmware.bin`

## 2. Subir el código a GitHub
Sube tus cambios al repositorio como haces normalmente:
```bash
git add .
git commit -m "Descripción de las mejoras"
git push
```

## 3. Crear una "Release" en GitHub (El paso clave)
Para que el ESP32 detecte la actualización, debes "publicarla" en GitHub:
1.  Ve a tu repositorio: [Karimbelmonte/Pantalla-EMT](https://github.com/Karimbelmonte/Pantalla-EMT)
2.  En la barra lateral derecha, haz clic en **"Releases"** -> **"Create a new release"**.
3.  **Choose a tag:** Escribe la versión con una 'v' delante, por ejemplo: `v0.11`.
4.  **Título:** Pon un nombre a la versión (ej: `Mejora de fuentes`).
5.  **ADJUNTA EL BINARIO:** Arrastra el archivo `firmware.bin` (el que localizaste en el punto 1) al recuadro de "Attach binaries".
6.  Haz clic en **"Publish release"**.

## 4. ¡Listo!
La próxima vez que reinicies tu pantalla (o al encenderla por la mañana), ella:
- Se conectará a GitHub.
- Verá que existe la `v0.11`.
- Comparará con su versión actual (`0.10`).
- Se descargará el archivo sola y se reiniciará actualizada.

---
> [!TIP]
> Si quieres forzar la actualización sin esperar, simplemente pulsa el botón de **Reiniciar** en la web de configuración de tu pantalla.
