#include "globals.h"
#include "web_server.h"
#include <WiFi.h>

#include "firmware_version.h"
#include "wifi_connection.h"
#include "boot_diagnostics.h"
#include "ota_control.h"
#include <esp_random.h>

static String htmlHeader(const char* title);
static String htmlFooter();
static char otaFormToken[33] = {};
static String htmlEscape(String value) {
  value.replace("&", "&amp;"); value.replace("<", "&lt;"); value.replace(">", "&gt;");
  value.replace("\"", "&quot;"); value.replace("'", "&#39;");
  return value;
}

static void sendResponse(int code, const String& msg) {
  server.send(code, contentTypeText, msg);
}

static void handleInfo() {
  uint32_t uptime = millis();
  int d = uptime / 86400000UL;
  int h = (uptime % 86400000UL) / 3600000UL;
  int m = ((uptime % 86400000UL) % 3600000UL) / 60000UL;
  char uptimeStr[64];
  snprintf(uptimeStr, sizeof(uptimeStr), "%dd %dh %dm", d, h, m);

  String s = htmlHeader("Información del Sistema");
  s += "<body>\n";
  s += "  <div class=\"container\">\n";
  s += "    <div class=\"header\">\n";
  s += "      <h1>Estado del Sistema ⚙️</h1>\n";
  s += "    </div>\n";

  s += "    <div class=\"card\">\n";
  s += "      <h3>General</h3>\n";
  s += "<p>Mayor intervalo del bucle: " + String(maxLoopGapMs) + " ms</p>";
  s += "<p>Firmware: " FIRMWARE_VERSION "</p>";
  s += "<p>Motivo de este arranque: " + htmlEscape(String(bootResetReason())) + "</p>";
  s += "<p>Operación anterior al reinicio: " + htmlEscape(String(bootPreviousStage())) + "</p>";
  s += "<p>Reinicio solicitado: " + htmlEscape(String(bootPreviousIntent())) + "</p>";
  if (bootHasPreviousTrace()) s += "<p>Tiempo activo anterior: " + String(bootPreviousUptimeMs()/1000) + " s</p>";
  s += "<p>WiFi al arrancar: " + htmlEscape(String(wifiCredentialsAtBoot())) + "</p>";
  s += "<p>Desconexiones WiFi en este arranque: " + String(wifiDisconnectCount()) + "</p>";
  s += "<p>Último código de desconexión WiFi: " + String(wifiLastDisconnectReason()) + "</p>";
  if (!wifiConnected) s += "<p>Reintento WiFi en: " + String(wifiRetryInMs()/1000) + " s</p>";

  s += "<p>OTA: " + htmlEscape(otaStatus) + "</p>";
  s += "<p>Instalación automática: " + String(ENABLE_AUTO_OTA ? "activada" : "desactivada") + "</p>";
  s += "<p><a href=\"/update\">Actualizar por WiFi</a></p>";
  s += "<p>Configuración: " + (configError.isEmpty() ? String("OK") : htmlEscape(configError)) + "</p>";
  s += "      <p><b>WiFi:</b> " + String(wifiConnected ? "Conectado ✅" : "Desconectado ❌") + "</p>\n";
  s += "      <p><b>RSSI:</b> " + String(WiFi.RSSI()) + " dB</p>\n";
  s += "      <p><b>Uptime:</b> " + String(uptimeStr) + "</p>\n";
  s += "      <p><b>Estado App:</b> " + String((appState == ST_RUNNING) ? "Ejecutando" : (appState == ST_NEED_STOP ? "Necesita Parada" : "Iniciando")) + "</p>\n";
  s += "      <p><b>Ciclo UI:</b> " + String(uiScreen == UI_BUS ? "Bus 1" : (uiScreen == UI_WEATHER ? "Clima" : "Bus 2")) + "</p>\n";
  s += "      <p><b>Brillo Actual:</b> " + String(brightness) + "</p>\n";
  s += "    </div>\n";

  s += "    <div class=\"card\">\n";
  s += "      <h3>EMT Madrid</h3>\n";
  s += "      <p><b>Parada:</b> " + htmlEscape(cfg.emtStopId) + " (" + htmlEscape(String(displayStopName())) + ")</p>\n";
  s += "      <p><b>Token:</b> " + String(emtAccessToken.isEmpty() ? "No disponible ❌" : "Activo ✅") + "</p>\n";
  if (!emtLastErrorMsg.isEmpty()) {
    s += "      <p style=\"color: #e74c3c;\"><b>Último Error:</b> " + htmlEscape(emtLastErrorMsg) + "</p>\n";
  }
  s += "    </div>\n";

  s += "    <div class=\"card\">\n";
  s += "      <h3>OpenWeather</h3>\n";
  s += "      <p><b>Datos Clima:</b> " + String(weather.hasData ? "Disponibles ✅" : "No disponibles ❌") + "</p>\n";
  if (weather.sunrise > 0) {
    char buf[64];
    time_t sr = (time_t)weather.sunrise;
    struct tm *tm_sr = localtime(&sr);
    strftime(buf, sizeof(buf), "%H:%M:%S", tm_sr);
    s += "      <p><b>Amanecer:</b> " + String(buf) + "</p>\n";

    time_t ss = (time_t)weather.sunset;
    struct tm *tm_ss = localtime(&ss);
    strftime(buf, sizeof(buf), "%H:%M:%S", tm_ss);
    s += "      <p><b>Atardecer:</b> " + String(buf) + "</p>\n";
  }
  if (!weather.lastError.isEmpty()) {
    s += "      <p style=\"color: #e74c3c;\"><b>Error Clima:</b> " + htmlEscape(weather.lastError) + "</p>\n";
  }
  s += "    </div>\n";

  s += "    <div class=\"card\">\n";
  s += "      <h3>Estadísticas</h3>\n";
  s += "<p>Próximo detalle EMT: " + String(stopRetry.remaining(millis())/1000) + " s</p>";
  s += "<p>Próximo intento clima: " + String(weatherRetry.remaining(millis())/1000) + " s</p>";
  s += "<p>Espera autenticación EMT: " + String(authRetry.remaining(millis())/1000) + " s</p>";
  s += "<p>Edad clima: " + (weather.hasData ? String((millis()-weather.lastFetchMs)/1000) + " s" : String("Sin datos")) + "</p>";

  s += "      <p><b>Cargas OK:</b> " + String(dataLoadSuccess) + "</p>\n";
  s += "      <p><b>Fallos:</b> " + String(dataLoadFailure) + "</p>\n";
  s += "      <p><b>Última Carga:</b> " + (dataLoadSuccess > 0 ? String((millis() - lastDataLoadTime)/1000) + " s" : String("Sin datos")) + "</p>\n";
  s += "    </div>\n";

  s += "    <a href=\"/\" class=\"btn-submit\" style=\"text-decoration:none; display:block; text-align:center; margin-top:20px;\">Volver al Inicio</a>\n";

  s += "  </div>\n";
  s += htmlFooter();
  server.send(200, contentTypeHtml, s);
}

static void handleReboot() {
  recordRestartIntent(emt::RestartIntent::Web);
  sendResponse(200, "Reiniciando...");
  delay(400);
  ESP.restart();
}

static void handleBrightness() {
  if (!server.hasArg("b")) {
    sendResponse(400, "Falta parametro b");
    return;
  }
  int level = server.arg("b").toInt();
  if (level < 1 || level > 255) {
    sendResponse(400, "b debe estar entre 1 y 255");
    return;
  }
  brightness = level;
  u8g2.setContrast(brightness);
  sendResponse(200, "OK");
}

static void handleSleepNow() {
  if (!cfg.sleepEnabled) {
    sendResponse(400, "Sleep no habilitado");
    return;
  }
  if (!cfg.sleepDeep) {
    sendResponse(200, "SleepNow solo hace deep sleep si sleepDeep=YES. Cambia modo en /config.");
    return;
  }
  sendResponse(200, "Entrando en deep sleep...");
  delay(200);
  enterDeepSleepUntilWake();
}

static String htmlHeader(const char* title) {
  String s;
  s.reserve(4096); // RESERVA para evitar fragmentacion
  s += "<!doctype html>\n";
  s += "<html lang=\"es\">\n";
  s += "<head>\n";
  s += "    <meta charset=\"utf-8\">\n";
  s += "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n";
  s += "    <title>" + htmlEscape(String(title)) + "</title>\n";
  s += "    <link href=\"https://fonts.googleapis.com/css2?family=Outfit:wght@300;400;600;700&display=swap\" rel=\"stylesheet\">\n";
  s += "    <style>\n";
  s += "        /* Estilos base: Solar Punk & Transporte */\n";
  s += "        :root {\n";
  s += "            --bg-gradient: linear-gradient(135deg, #FFF9E6 0%, #E2F3E8 100%);\n";
  s += "            --card-bg: rgba(255, 255, 255, 0.75);\n";
  s += "            --card-border: rgba(255, 255, 255, 0.5);\n";
  s += "            --primary: #2D6A4F;\n";
  s += "            /* Verde orgánico oscuro */\n";
  s += "            --secondary: #D4A373;\n";
  s += "            /* Tono madera clara / dorado cálido */\n";
  s += "            --accent: #FFB703;\n";
  s += "            /* Amarillo sol */\n";
  s += "            --text-main: #1B4332;\n";
  s += "            /* Texto oscuro verde */\n";
  s += "            --text-muted: #52796F;\n";
  s += "            --shadow: 0 8px 32px rgba(45, 106, 79, 0.1);\n";
  s += "            --radius: 16px;\n";
  s += "        }\n";
  s += "        body {\n";
  s += "            font-family: 'Outfit', system-ui, sans-serif;\n";
  s += "            background: var(--bg-gradient);\n";
  s += "            color: var(--text-main);\n";
  s += "            margin: 0;\n";
  s += "            padding: 0;\n";
  s += "            display: flex;\n";
  s += "            justify-content: center;\n";
  s += "            min-height: 100vh;\n";
  s += "        }\n";
  s += "        .container {\n";
  s += "            width: 100%;\n";
  s += "            max-width: 600px;\n";
  s += "            margin: 40px 20px;\n";
  s += "        }\n";
  s += "        /* Header */\n";
  s += "        .header {\n";
  s += "            text-align: center;\n";
  s += "            margin-bottom: 32px;\n";
  s += "        }\n";
  s += "        .header h1 {\n";
  s += "            font-size: 2.2rem;\n";
  s += "            font-weight: 700;\n";
  s += "            margin: 0;\n";
  s += "            color: var(--primary);\n";
  s += "            display: flex;\n";
  s += "            align-items: center;\n";
  s += "            justify-content: center;\n";
  s += "            gap: 12px;\n";
  s += "        }\n";
  s += "        .header h1 svg {\n";
  s += "            width: 36px;\n";
  s += "            height: 36px;\n";
  s += "            fill: var(--accent);\n";
  s += "        }\n";
  s += "        .status-badge {\n";
  s += "            display: inline-flex;\n";
  s += "            align-items: center;\n";
  s += "            gap: 8px;\n";
  s += "            background: var(--card-bg);\n";
  s += "            backdrop-filter: blur(10px);\n";
  s += "            padding: 8px 16px;\n";
  s += "            border-radius: 20px;\n";
  s += "            font-size: 0.9rem;\n";
  s += "            font-weight: 600;\n";
  s += "            color: var(--primary);\n";
  s += "            margin-top: 16px;\n";
  s += "            box-shadow: 0 4px 12px rgba(0, 0, 0, 0.05);\n";
  s += "            border: 1px solid var(--card-border);\n";
  s += "        }\n";
  s += "        .status-dot {\n";
  s += "            width: 10px;\n";
  s += "            height: 10px;\n";
  s += "            background-color: #52B788;\n";
  s += "            border-radius: 50%;\n";
  s += "            box-shadow: 0 0 8px #52B788;\n";
  s += "        }\n";
  s += "        /* Tarjetas (Cards) */\n";
  s += "        form {\n";
  s += "            display: flex;\n";
  s += "            flex-direction: column;\n";
  s += "            gap: 24px;\n";
  s += "        }\n";
  s += "        .card {\n";
  s += "            background: var(--card-bg);\n";
  s += "            backdrop-filter: blur(16px);\n";
  s += "            -webkit-backdrop-filter: blur(16px);\n";
  s += "            border: 1px solid var(--card-border);\n";
  s += "            border-radius: var(--radius);\n";
  s += "            padding: 24px;\n";
  s += "            box-shadow: var(--shadow);\n";
  s += "            transition: transform 0.2s ease, box-shadow 0.2s ease;\n";
  s += "        }\n";
  s += "        .card:hover {\n";
  s += "            box-shadow: 0 12px 40px rgba(45, 106, 79, 0.15);\n";
  s += "        }\n";
  s += "        .card h3 {\n";
  s += "            margin-top: 0;\n";
  s += "            margin-bottom: 20px;\n";
  s += "            font-size: 1.3rem;\n";
  s += "            font-weight: 600;\n";
  s += "            color: var(--primary);\n";
  s += "            display: flex;\n";
  s += "            align-items: center;\n";
  s += "            gap: 10px;\n";
  s += "            border-bottom: 2px solid rgba(212, 163, 115, 0.2);\n";
  s += "            padding-bottom: 12px;\n";
  s += "        }\n";
  s += "        .card h3 svg {\n";
  s += "            width: 24px;\n";
  s += "            height: 24px;\n";
  s += "            fill: var(--secondary);\n";
  s += "        }\n";
  s += "        /* Inputs */\n";
  s += "        .form-group {\n";
  s += "            margin-bottom: 16px;\n";
  s += "        }\n";
  s += "        .form-group:last-child {\n";
  s += "            margin-bottom: 0;\n";
  s += "        }\n";
  s += "        label {\n";
  s += "            display: block;\n";
  s += "            font-size: 0.95rem;\n";
  s += "            font-weight: 600;\n";
  s += "            margin-bottom: 8px;\n";
  s += "            color: var(--text-muted);\n";
  s += "        }\n";
  s += "        input[type='text'],\n";
  s += "        input[type='password'],\n";
  s += "        input[type='number'],\n";
  s += "        select {\n";
  s += "            width: 100%;\n";
  s += "            padding: 12px 16px;\n";
  s += "            font-family: 'Outfit', sans-serif;\n";
  s += "            font-size: 1rem;\n";
  s += "            color: var(--text-main);\n";
  s += "            background: rgba(255, 255, 255, 0.9);\n";
  s += "            border: 2px solid transparent;\n";
  s += "            border-radius: 8px;\n";
  s += "            box-sizing: border-box;\n";
  s += "            outline: none;\n";
  s += "            transition: border-color 0.3s ease, box-shadow 0.3s ease;\n";
  s += "            box-shadow: inset 0 2px 4px rgba(0, 0, 0, 0.02);\n";
  s += "        }\n";
  s += "        input[type='text']:focus,\n";
  s += "        input[type='password']:focus,\n";
  s += "        input[type='number']:focus,\n";
  s += "        select:focus {\n";
  s += "            border-color: var(--secondary);\n";
  s += "            box-shadow: 0 0 0 4px rgba(212, 163, 115, 0.1);\n";
  s += "        }\n";
  s += "        /* Checkbox estilo Toggle (Switch) */\n";
  s += "        .checkbox-wrapper {\n";
  s += "            display: flex;\n";
  s += "            align-items: center;\n";
  s += "            gap: 12px;\n";
  s += "            cursor: pointer;\n";
  s += "        }\n";
  s += "        .checkbox-wrapper input[type=\"checkbox\"] {\n";
  s += "            appearance: none;\n";
  s += "            width: 48px;\n";
  s += "            height: 24px;\n";
  s += "            background: rgba(27, 67, 50, 0.2);\n";
  s += "            border-radius: 12px;\n";
  s += "            position: relative;\n";
  s += "            cursor: pointer;\n";
  s += "            outline: none;\n";
  s += "            transition: background 0.3s;\n";
  s += "        }\n";
  s += "        .checkbox-wrapper input[type=\"checkbox\"]::after {\n";
  s += "            content: '';\n";
  s += "            position: absolute;\n";
  s += "            top: 2px;\n";
  s += "            left: 2px;\n";
  s += "            width: 20px;\n";
  s += "            height: 20px;\n";
  s += "            background: white;\n";
  s += "            border-radius: 50%;\n";
  s += "            transition: transform 0.3s;\n";
  s += "            box-shadow: 0 2px 4px rgba(0, 0, 0, 0.2);\n";
  s += "        }\n";
  s += "        .checkbox-wrapper input[type=\"checkbox\"]:checked {\n";
  s += "            background: var(--primary);\n";
  s += "        }\n";
  s += "        .checkbox-wrapper input[type=\"checkbox\"]:checked::after {\n";
  s += "            transform: translateX(24px);\n";
  s += "        }\n";
  s += "        .checkbox-label {\n";
  s += "            font-weight: 600;\n";
  s += "            color: var(--text-main);\n";
  s += "        }\n";
  s += "        /* Footer & Buttons */\n";
  s += "        .btn-submit {\n";
  s += "            width: 100%;\n";
  s += "            background: var(--primary);\n";
  s += "            color: white;\n";
  s += "            border: none;\n";
  s += "            padding: 16px;\n";
  s += "            font-size: 1.1rem;\n";
  s += "            font-weight: 700;\n";
  s += "            font-family: inherit;\n";
  s += "            border-radius: var(--radius);\n";
  s += "            cursor: pointer;\n";
  s += "            box-shadow: 0 4px 15px rgba(45, 106, 79, 0.3);\n";
  s += "            transition: background 0.3s, transform 0.1s, box-shadow 0.3s;\n";
  s += "            margin-top: 10px;\n";
  s += "        }\n";
  s += "        .btn-submit:hover {\n";
  s += "            background: #1B4332;\n";
  s += "            box-shadow: 0 6px 20px rgba(45, 106, 79, 0.4);\n";
  s += "            transform: translateY(-2px);\n";
  s += "        }\n";
  s += "        .btn-submit:active {\n";
  s += "            transform: translateY(0);\n";
  s += "        }\n";
  s += "        .footer-links {\n";
  s += "            margin-top: 32px;\n";
  s += "            display: flex;\n";
  s += "            justify-content: center;\n";
  s += "            gap: 16px;\n";
  s += "            flex-wrap: wrap;\n";
  s += "        }\n";
  s += "        .footer-links a {\n";
  s += "            color: var(--primary);\n";
  s += "            text-decoration: none;\n";
  s += "            font-weight: 600;\n";
  s += "            font-size: 0.9rem;\n";
  s += "            padding: 8px 16px;\n";
  s += "            border-radius: 20px;\n";
  s += "            background: rgba(255, 255, 255, 0.5);\n";
  s += "            border: 1px solid var(--card-border);\n";
  s += "            transition: all 0.2s;\n";
  s += "        }\n";
  s += "        .footer-links a:hover {\n";
  s += "            background: var(--primary);\n";
  s += "            color: white;\n";
  s += "            border-color: var(--primary);\n";
  s += "        }\n";
  s += "        /* Responsive */\n";
  s += "        @media (max-width: 480px) {\n";
  s += "            .card {\n";
  s += "                padding: 20px;\n";
  s += "            }\n";
  s += "            .header h1 {\n";
  s += "                font-size: 1.8rem;\n";
  s += "            }\n";
  s += "        }\n";
  s += "        /* Toast Notification */\n";
  s += "        #toast {\n";
  s += "            visibility: hidden;\n";
  s += "            min-width: 250px;\n";
  s += "            background-color: var(--primary);\n";
  s += "            color: #fff;\n";
  s += "            text-align: center;\n";
  s += "            border-radius: var(--radius);\n";
  s += "            padding: 16px;\n";
  s += "            position: fixed;\n";
  s += "            z-index: 1000;\n";
  s += "            left: 50%;\n";
  s += "            bottom: 30px;\n";
  s += "            transform: translateX(-50%);\n";
  s += "            box-shadow: 0 10px 30px rgba(0,0,0,0.2);\n";
  s += "            font-weight: 600;\n";
  s += "            transition: visibility 0s, opacity 0.5s linear;\n";
  s += "            opacity: 0;\n";
  s += "        }\n";
  s += "        #toast.show {\n";
  s += "            visibility: visible;\n";
  s += "            opacity: 1;\n";
  s += "        }\n";
  s += "    </style>\n";
  s += "</head>\n";
  return s;
}

static String htmlFooter() {
  return "</body></html>";
}

static void handleConfigGet() {
  String s = htmlHeader("Pantalla EMT");
  s += "<body style=\"margin:0;padding:0;\">\n";
  s += "<div class=\"container\">\n";
  s += "  <div class=\"header\">\n";
  s += "    <h1><svg viewBox=\"0 0 24 24\"><path d=\"M12 2L15.09 5.09L19.14 4.14L20.09 8.19L23.18 11.28L20.09 14.37L19.14 18.42L15.09 17.47L12 20.56L8.91 17.47L4.86 18.42L3.91 14.37L0.82 11.28L3.91 8.19L4.86 4.14L8.91 5.09L12 2Z\"></path></svg> Pantalla EMT</h1>\n";
  s += "    <div class=\"status-badge\">\n";
  s += "      <div class=\"status-dot\"></div>\n";
  s += "      " + WiFi.localIP().toString() + " &nbsp;|&nbsp; pantalla-emt.local\n";
  s += "    </div>\n";
  s += "  </div>\n";
  s += "<form method=\"POST\" action=\"/config\">\n";
  s += "    <div class=\"card\">\n";
  s += "      <h3>\n";
  s += "        <svg viewBox=\"0 0 24 24\"><path d=\"M4 16C4 17.1 4.9 18 6 18H6.5V20C6.5 20.55 6.95 21 7.5 21C8.05 21 8.5 20.55 8.5 20V18H15.5V20C15.5 20.55 15.95 21 16.5 21C17.05 21 17.5 20.55 17.5 20V18H18C19.1 18 20 17.1 20 16V6C20 3.5 16.42 2 12 2C7.58 2 4 3.5 4 6V16ZM7.5 13C6.67 13 6 12.33 6 11.5C6 10.67 6.67 10 7.5 10C8.33 10 9 10.67 9 11.5C9 12.33 8.33 13 7.5 13ZM16.5 13C15.67 13 15 12.33 15 11.5C15 10.67 15.67 10 16.5 10C17.33 10 18 10.67 18 11.5C18 12.33 17.33 13 16.5 13ZM18 7H6V6C6 4.9 8.69 4 12 4C15.31 4 18 4.9 18 6V7Z\"></path></svg>\n";
  s += "        Parada de Autobús\n";
  s += "      </h3>\n";
  s += "      <div class=\"form-group\">\n";
  s += "        <label>ID de la Parada (StopId)</label>\n";
  s += "        <input type=\"number\" name=\"stopId\" value=\"" + htmlEscape(cfg.emtStopId) + "\">\n";
  s += "      </div>\n";
  s += "      <div class=\"form-group\">\n";
  s += "        <label>Nombre de Parada (Opcional ✨)</label>\n";
  s += "        <input type=\"text\" name=\"stopName\" value=\"" + htmlEscape(cfg.emtStopName) + "\" placeholder=\"Ej: Cibeles (Dejar vacío para auto)\">\n";
  s += "      </div>\n";
  s += "    </div>\n";
  s += "    <div class=\"card\">\n";
  s += "      <h3>\n";
  s += "        <svg viewBox=\"0 0 24 24\"><path d=\"M12 1L3 5V11C3 16.55 6.84 21.74 12 23C17.16 21.74 21 16.55 21 11V5L12 1ZM12 11.99H19C18.47 16.11 15.72 19.78 12 20.92V12H5V6.3L12 3.19V11.99Z\"></path></svg>\n";
  s += "        Credenciales MobilityLabs\n";
  s += "      </h3>\n";
  s += "      <div class=\"form-group\">\n";
  s += "        <label>Correo Electrónico</label>\n";
  s += "        <input type=\"text\" name=\"emtEmail\" value=\"" + htmlEscape(cfg.emtEmail) + "\">\n";
  s += "      </div>\n";
  s += "      <div class=\"form-group\">\n";
  s += "        <label>Contraseña</label>\n";
  s += "        <input type=\"password\" name=\"emtPassword\" value=\"" + htmlEscape(cfg.emtPassword) + "\">\n";
  s += "      </div>\n";
  s += "    </div>\n";
  s += "    <div class=\"card\">\n";
  s += "      <h3>\n";
  s += "        <svg viewBox=\"0 0 24 24\"><path d=\"M6.76 4.84L5.34 3.43L4.93 3.84C3.12 5.65 2.11 8.05 2.11 10.61C2.11 15.86 6.36 20.11 11.61 20.11C14.17 20.11 16.57 19.1 18.38 17.29L19.79 18.7L21.2 17.29L8.17 4.25L6.76 4.84ZM11.61 18.11C7.47 18.11 4.11 14.75 4.11 10.61C4.11 8.59 4.9 6.75 6.18 5.41L16.81 16.04C15.47 17.32 13.63 18.11 11.61 18.11Z\"></path><path d=\"M19.89 10.61C19.89 8.35 18.98 6.25 17.38 4.65L15.97 6.06C17.17 7.27 17.89 8.88 17.89 10.61C17.89 12.34 17.17 13.95 15.97 15.16L17.38 16.57C18.98 14.97 19.89 12.87 19.89 10.61Z\"></path></svg>\n";
  s += "        Clima & Entorno\n";
  s += "      </h3>\n";
  s += "      <div class=\"form-group\">\n";
  s += "        <label>OpenWeather API Key</label>\n";
  s += "        <input type=\"text\" name=\"weatherKey\" value=\"" + htmlEscape(cfg.weatherApiKey) + "\">\n";
  s += "      </div>\n";
  s += "    </div>\n";
  s += "    <div class=\"card\">\n";
  s += "      <h3>\n";
  s += "        <svg viewBox=\"0 0 24 24\"><path d=\"M21 3H3C1.9 3 1 3.9 1 5V19C1 20.1 1.9 21 3 21H21C22.1 21 23 20.1 23 19V5C23 3.9 22.1 3 21 3ZM21 19H3V5H21V19Z\"></path></svg>\n";
  s += "        OLED & Tiempos\n";
  s += "      </h3>\n";
  s += "      <div class=\"form-group\">\n";
  s += "        <label>Brillo manual guardado (1-255)</label>\n";
  s += "        <input type=\"number\" name=\"brightness\" value=\"" + String(cfg.manualBrightness) + "\">\n";
  s += "      </div>\n";
  s += "      <div style=\"display:flex; gap:16px;\">\n";
  s += "        <div class=\"form-group\" style=\"flex:1;\">\n";
  s += "          <label>Buses (seg)</label>\n";
  s += "          <input type=\"number\" name=\"busSecs\" id=\"busSecs\" value=\"" + String(cfg.uiBusMs / 1000UL) + "\" oninput=\"validateBus2()\">\n";
  s += "        </div>\n";
  s += "        <div class=\"form-group\" style=\"flex:1;\">\n";
  s += "          <label>Tiempo (seg)</label>\n";
  s += "          <input type=\"number\" name=\"weatherSecs\" value=\"" + String(cfg.uiWeatherMs / 1000UL) + "\">\n";
  s += "        </div>\n";
  s += "      </div>\n";
  s += "      <div class=\"form-group\">\n";
  s += "        <label>Bus pág.2 (seg, 0=desactivado)</label>\n";
  s += "        <input type=\"number\" name=\"busSecs2\" id=\"busSecs2\" value=\"" + String(cfg.uiBus2Ms / 1000UL) + "\" min=\"0\" oninput=\"validateBus2()\">\n";
  s += "        <p id=\"bus2Error\" style=\"color:#e53e3e;display:none;font-size:0.85rem;margin:6px 0 0;padding:8px 12px;background:rgba(229,62,62,0.08);border-radius:8px;border:1px solid rgba(229,62,62,0.25);\">\n          ⚠️ Para aumentar el tiempo de la segunda pantalla, deja al menos 5 segundos para la primera pantalla de bus.\n        </p>\n";
  s += "      </div>\n";
  s += "    </div>\n";
  s += "    <div class=\"card\">\n";
  s += "      <h3>\n";
  s += "        <svg viewBox=\"0 0 24 24\"><path d=\"M2.01 21L23 12L2.01 3L2 10L17 12L2 14L2.01 21ZM12 8.5C12 6.57 10.43 5 8.5 5C6.57 5 5 6.57 5 8.5C5 10.43 6.57 12 8.5 12C10.43 12 12 10.43 12 8.5Z\"></path></svg>\n";
  s += "        Opciones Visuales\n";
  s += "      </h3>\n";
  s += "      <div class=\"form-group\">\n";
  s += "        <label class=\"checkbox-wrapper\">\n";
  s += "          <input type=\"checkbox\" name=\"combineArrivals\" <!-- checked_combine_arrivals --> >\n";
  s += "          <span class=\"checkbox-label\">Agrupar tiempos (2 por fila)</span>\n";
  s += "        </label>\n";
  s += "      </div>\n";
  s += "    </div>\n";
  s += "    <div class=\"card\">\n";
  s += "      <h3>\n";
  s += "        <svg viewBox=\"0 0 24 24\"><path d=\"M2.01 21L23 12L2.01 3L2 10L17 12L2 14L2.01 21ZM12 8.5C12 6.57 10.43 5 8.5 5C6.57 5 5 6.57 5 8.5C5 10.43 6.57 12 8.5 12C10.43 12 12 10.43 12 8.5Z\"></path></svg>\n";
  s += "        Ahorro Energético\n";
  s += "      </h3>\n";
  s += "      <div class=\"form-group\">\n";
  s += "        <label class=\"checkbox-wrapper\">\n";
  s += "          <input type=\"checkbox\" name=\"sleepEnabled\"" + String(cfg.sleepEnabled ? " checked" : "") + ">\n";
  s += "          <span class=\"checkbox-label\">Habilitar Horario de Sueño</span>\n";
  s += "        </label>\n";
  s += "      </div>\n";
  s += "      <div class=\"form-group\">\n";
  s += "        <label>Modo de Reposo</label>\n";
  s += "        <select name=\"sleepMode\">\n";
  s += "          <option value=\"soft\"" + String(!cfg.sleepDeep ? " selected" : "") + ">Soft (Pantalla tenue, WiFi activo)</option>\n";
  s += "          <option value=\"deep\"" + String(cfg.sleepDeep ? " selected" : "") + ">Deep Sleep (Apagado completo, ahorra ecosistema)</option>\n";
  s += "        </select>\n";
  s += "      </div>\n";
  s += "      <div style=\"display:flex; gap:16px;\">\n";
  s += "        <div class=\"form-group\" style=\"flex:1;\">\n";
  s += "          <label>Apagar a las (0-23h)</label>\n";
  s += "          <input type=\"number\" name=\"sleepStarts\" value=\"" + String(cfg.sleepStarts) + "\" min=\"0\" max=\"23\">\n";
  s += "        </div>\n";
  s += "        <div class=\"form-group\" style=\"flex:1;\">\n";
  s += "          <label>Despertar a las (0-23h)</label>\n";
  s += "          <input type=\"number\" name=\"sleepEnds\" value=\"" + String(cfg.sleepEnds) + "\" min=\"0\" max=\"23\">\n";
  s += "        </div>\n";
  s += "      </div>\n";
  s += "    </div>\n";
  s += "    <div class=\"card\">\n";
  s += "      <h3>\n";
  s += "        <svg viewBox=\"0 0 24 24\"><path d=\"M12 7c-2.76 0-5 2.24-5 5s2.24 5 5 5 5-2.24 5-5-2.24-5-5-5zM2 13h2c.55 0 1-.45 1-1s-.45-1-1-1H2c-.55 0-1 .45-1 1s.45 1 1 1zm18 0h2c.55 0 1-.45 1-1s-.45-1-1-1h-2c-.55 0-1 .45-1 1s.45 1 1 1zM11 2v2c0 .55.45 1 1 1s1-.45 1-1V2c0-.55-.45-1-1-1s-1 .45-1 1zm0 18v2c0 .55.45 1 1 1s1-.45 1-1v-2c0-.55-.45-1-1-1s-1 .45-1 1zM5.99 4.58c-.39-.39-1.03-.39-1.41 0s-.39 1.03 0 1.41l1.06 1.06c.39.39 1.03.39 1.41 0s.39-1.03 0-1.41L5.99 4.58zm12.37 12.37c-.39-.39-1.03-.39-1.41 0s-.39 1.03 0 1.41l1.06 1.06c.39.39 1.03.39 1.41 0s.39-1.03 0-1.41l-1.06-1.06zm1.06-10.96c.39-.39.39-1.03 0-1.41s-1.03-.39-1.41 0l-1.06 1.06c-.39.39-.39 1.03 0 1.41s1.03.39 1.41 0l1.06-1.06zM7.05 18.36c.39-.39.39-1.03 0-1.41s-1.03-.39-1.41 0l-1.06 1.06c-.39.39-.39 1.03 0 1.41s1.03.39 1.41 0l1.06-1.06z\"></path></svg>\n";
  s += "        Auto Brillo (Sunrise/Sunset)\n";
  s += "      </h3>\n";
  s += "      <div class=\"form-group\">\n";
  s += "        <label class=\"checkbox-wrapper\">\n";
  s += "          <input type=\"checkbox\" name=\"autoBrightness\"" + String(cfg.autoBrightness ? " checked" : "") + ">\n";
  s += "          <span class=\"checkbox-label\">Habilitar Brillo Automático</span>\n";
  s += "        </label>\n";
  s += "      </div>\n";
  s += "      <div style=\"display:flex; gap:16px;\">\n";
  s += "        <div class=\"form-group\" style=\"flex:1;\">\n";
  s += "          <label>Brillo Día (1-255)</label>\n";
  s += "          <input type=\"number\" name=\"brightnessDay\" value=\"" + String(cfg.brightnessDay) + "\" min=\"1\" max=\"255\">\n";
  s += "        </div>\n";
  s += "        <div class=\"form-group\" style=\"flex:1;\">\n";
  s += "          <label>Brillo Noche (1-255)</label>\n";
  s += "          <input type=\"number\" name=\"brightnessNight\" value=\"" + String(cfg.brightnessNight) + "\" min=\"1\" max=\"255\">\n";
  s += "        </div>\n";
  s += "      </div>\n";
  s += "    </div>\n";
  s += "    <button type=\"submit\" id=\"btnSubmit\" class=\"btn-submit\">Guardar Configuración 🌿</button>\n";
  s += "<input type=\"hidden\" name=\"fullForm\" value=\"1\">";
  s += "  </form>\n";
  s += "<p><a href=\"/update\" class=\"btn-submit\" style=\"display:block;text-align:center;text-decoration:none\">Actualizar por WiFi</a></p>";
  s += "<form method=\"POST\" action=\"/wifi/setup\" style=\"margin-top:16px\"><button type=\"submit\" class=\"btn-submit\">Cambiar red WiFi</button></form>";
  s += "  <div id=\"toast\">Configuración guardada correctamente ✨</div>\n";
  s += "  <script>\n";
  s += "    function validateBus2() {\n";
  s += "      var bus = parseInt(document.getElementById('busSecs').value) || 0;\n";
  s += "      var bus2 = parseInt(document.getElementById('busSecs2').value) || 0;\n";
  s += "      var err = document.getElementById('bus2Error');\n";
  s += "      var inp = document.getElementById('busSecs2');\n";
  s += "      var btn = document.getElementById('btnSubmit');\n";
  s += "      var invalid = bus2 > Math.max(0, bus - 5);\n";
  s += "      err.style.display = invalid ? 'block' : 'none';\n";
  s += "      inp.style.borderColor = invalid ? '#e53e3e' : '';\n";
  s += "      inp.style.boxShadow = invalid ? '0 0 0 4px rgba(229,62,62,0.15)' : '';\n";
  s += "      btn.disabled = invalid;\n";
  s += "      btn.style.opacity = invalid ? '0.5' : '';\n";
  s += "      btn.style.cursor = invalid ? 'not-allowed' : '';\n";
  s += "    }\n";
  s += "    // Initial validation on page load\n";
  s += "    window.onload = validateBus2;\n";
  s += "    document.querySelector('form').onsubmit = function(e) {\n";
  s += "      e.preventDefault();\n";
  s += "      var btn = document.getElementById('btnSubmit');\n";
  s += "      var oldText = btn.innerText;\n";
  s += "      btn.disabled = true;\n";
  s += "      btn.innerText = 'Guardando...';\n";
  s += "      var formData = new FormData(this);\n";
  s += "      fetch('/config', {\n";
  s += "        method: 'POST',\n";
  s += "        body: formData\n";
  s += "      }).then(res => {\n";
  s += "        btn.disabled = false;\n";
  s += "        btn.innerText = oldText;\n";
  s += "        if (res.ok) {\n";
  s += "          var toast = document.getElementById('toast');\n";
  s += "          toast.className = 'show';\n";
  s += "          setTimeout(function() { toast.className = toast.className.replace('show', ''); }, 3000);\n";
  s += "        } else {\n";
  s += "          alert('Error al guardar la configuración');\n";
  s += "        }\n";
  s += "      }).catch(err => {\n";
  s += "        btn.disabled = false;\n";
  s += "        btn.innerText = oldText;\n";
  s += "        alert('Error de red al conectar con el dispositivo');\n";
  s += "      });\n";
  s += "    };\n";
  s += "  </script>\n";
  s.replace("<!-- checked_combine_arrivals -->", cfg.combineArrivals ? "checked" : "");

  s += "  <div class=\"card\" style=\"margin-top:24px; text-align:center;\">\n";
  s += "    <h3 style=\"margin-bottom:12px; justify-content:center; border-bottom:none;\">Acciones del Sistema</h3>\n";
  s += "    <p style=\"font-size:0.9rem; color:var(--text-muted); margin-bottom:20px;\">Controles avanzados para el diagnóstico y estado de la pantalla.</p>\n";
  s += "    <div class=\"footer-links\" style=\"flex-direction:column; gap:12px; margin-top:0;\">\n";
  s += "      <a href=\"/info\" style=\"display:flex; flex-direction:column; padding:12px;\">\n";
  s += "        <span style=\"font-size:1.1rem;\">/info Sistema</span>\n";
  s += "        <span style=\"font-size:0.8rem; font-weight:normal; opacity:0.8; margin-top:4px;\">Muestra versión, conexión WiFi, errores y antigüedad de los datos.</span>\n";
  s += "      </a>\n";
  s += "      <a href=\"/reboot\" style=\"display:flex; flex-direction:column; padding:12px;\">\n";
  s += "        <span style=\"font-size:1.1rem;\">Reiniciar Panel</span>\n";
  s += "        <span style=\"font-size:0.8rem; font-weight:normal; opacity:0.8; margin-top:4px;\">Fuerza un reinicio de hardware (como si lo desconectaras de la corriente).</span>\n";
  s += "      </a>\n";
  s += "      <a href=\"/sleepnow\" style=\"display:flex; flex-direction:column; padding:12px;\">\n";
  s += "        <span style=\"font-size:1.1rem;\">Forzar Deep Sleep</span>\n";
  s += "        <span style=\"font-size:0.8rem; font-weight:normal; opacity:0.8; margin-top:4px;\">Apaga la pantalla y entra en modo bajo consumo inmediatamente (solo si Deep Sleep está habilitado).</span>\n";
  s += "      </a>\n";
  s += "    </div>\n";
  s += "  </div>\n";
  s += "</div>\n";
  s += htmlFooter();
  server.send(200, contentTypeHtml, s);
}


static void handleConfigPost() {
  AppConfig candidate = cfg;
  auto textField = [&](const char* key, String& value) {
    if (server.hasArg(key)) { value = server.arg(key); value.trim(); }
  };
  textField("stopId", candidate.emtStopId);
  textField("stopName", candidate.emtStopName);
  textField("emtEmail", candidate.emtEmail);
  if (server.hasArg("emtPassword")) candidate.emtPassword = server.arg("emtPassword");
  textField("weatherKey", candidate.weatherApiKey);
  auto number = [&](const char* key, int current, int lo, int hi) {
    return server.hasArg(key) ? emt::bounded(static_cast<int>(server.arg(key).toInt()), lo, hi) : current;
  };
  candidate.manualBrightness = number("brightness", candidate.manualBrightness, 1, 255);
  candidate.brightnessDay = number("brightnessDay", candidate.brightnessDay, 1, 255);
  candidate.brightnessNight = number("brightnessNight", candidate.brightnessNight, 1, 255);
  candidate.uiBusMs = number("busSecs", candidate.uiBusMs/1000, 5, 300) * 1000UL;
  candidate.uiWeatherMs = number("weatherSecs", candidate.uiWeatherMs/1000, 3, 120) * 1000UL;
  candidate.uiBus2Ms = number("busSecs2", candidate.uiBus2Ms/1000, 0, 300) * 1000UL;
  candidate.sleepStarts = number("sleepStarts", candidate.sleepStarts, 0, 23);
  candidate.sleepEnds = number("sleepEnds", candidate.sleepEnds, 0, 23);
  // A marker distinguishes a full HTML form (unchecked boxes are absent)
  // from a partial API update, which must preserve omitted preferences.
  if (server.hasArg("fullForm") || server.hasArg("autoBrightness")) candidate.autoBrightness = server.hasArg("autoBrightness");
  if (server.hasArg("fullForm") || server.hasArg("combineArrivals")) candidate.combineArrivals = server.hasArg("combineArrivals");
  if (server.hasArg("fullForm") || server.hasArg("sleepEnabled")) candidate.sleepEnabled = server.hasArg("sleepEnabled");
  if (server.hasArg("sleepMode")) candidate.sleepDeep = server.arg("sleepMode") == "deep";
  emt::normalize(candidate);
  if (!emt::commitConfig(candidate, saveConfig, applyConfig)) {
    sendResponse(500, configError);
    return;
  }
  sendResponse(200, "OK");
}

static void handleWifiSetup() {
  sendResponse(200, "Abriendo configuración WiFi. Conecta a Pantalla EMT y abre 192.168.4.1. La red anterior no se borra por abrir el portal.");
  requestWifiSetup();
}

static void handleRoot() { handleConfigGet(); }

static void sendUpdatePage(int status = 200, const String& error = "") {
  String s = htmlHeader("Actualizar por WiFi");
  s += "<body><main class=\"container\"><h1>Actualizar por WiFi</h1><div class=\"card\">";
  s += "<p>Versión instalada: <b>" FIRMWARE_VERSION "</b></p>";
  s += "<p>Estado: " + htmlEscape(otaStatus) + "</p>";
  if (!error.isEmpty()) s += "<p role=\"alert\">" + htmlEscape(error) + "</p>";
  s += "<p>Busca una versión nueva y confirma para descargarla e instalarla. La pantalla se reiniciará al terminar. La actualización conserva la red WiFi y la configuración.</p>";
  if (otaBusy()) {
    s += "<p role=\"status\">Solicitud en curso. Mantén la alimentación. Durante la descarga la pantalla y esta web pueden tardar en responder.</p>";
    s += "<p><a href=\"/update\">Consultar resultado</a></p>";
    s += "<script>setTimeout(function(){location.replace('/update');},5000);</script>";
  } else {
    if (otaCanInstall()) {
      String version = htmlEscape(otaAvailableVersion());
      s += "<form method=\"POST\" action=\"/update/install\"><input type=\"hidden\" name=\"token\" value=\"" + String(otaFormToken) + "\">";
      s += "<input type=\"hidden\" name=\"version\" value=\"" + version + "\"><p>Disponible: <b>" + version + "</b></p>";
      s += "<button class=\"btn-submit\" type=\"submit\">Descargar e instalar " + version + "</button></form>";
    }
    s += "<form method=\"POST\" action=\"/update/check\" style=\"margin-top:16px\"><input type=\"hidden\" name=\"token\" value=\"" + String(otaFormToken) + "\">";
    s += "<button class=\"btn-submit\" type=\"submit\">Buscar actualización</button></form>";
  }
  s += "</div><p><a href=\"/\">Volver a configuración</a></p></main>";
  s += htmlFooter();
  server.sendHeader("Cache-Control", "no-store");
  server.send(status, contentTypeHtml, s);
}

static bool validUpdateForm() {
  if (otaFormToken[0] && server.hasArg("token") && server.arg("token") == otaFormToken) return true;
  sendUpdatePage(403, "Formulario caducado o no válido. Abre de nuevo esta página.");
  return false;
}

static void handleUpdateCheck() {
  if (!validUpdateForm()) return;
  if (!wifiConnected) { sendUpdatePage(503, "Sin conexión WiFi. Espera a que se reconecte."); return; }
  if (!requestOtaCheck()) { sendUpdatePage(409, "Ya hay una solicitud en curso."); return; }
  sendUpdatePage(202);
}

static void handleUpdateInstall() {
  if (!validUpdateForm()) return;
  if (!wifiConnected) { sendUpdatePage(503, "Sin conexión WiFi. Espera a que se reconecte."); return; }
  if (!requestOtaInstall(server.arg("version"))) {
    sendUpdatePage(409, "La versión ya no está disponible, la consulta caducó o hay otra solicitud en curso. Busca de nuevo.");
    return;
  }
  sendUpdatePage(202);
}

void setupWebServer() {
  uint8_t randomBytes[16];
  esp_fill_random(randomBytes, sizeof(randomBytes));
  for (size_t i = 0; i < sizeof(randomBytes); ++i) snprintf(otaFormToken + 2*i, 3, "%02x", randomBytes[i]);
  server.on("/", HTTP_GET, handleRoot);
  server.on("/info", HTTP_GET, handleInfo);
  server.on("/reboot", HTTP_GET, handleReboot);
  server.on("/brightness", HTTP_GET, handleBrightness);
  server.on("/sleepnow", HTTP_GET, handleSleepNow);
  server.on("/config", HTTP_GET, handleConfigGet);
  server.on("/config", HTTP_POST, handleConfigPost);
  server.on("/wifi/setup", HTTP_POST, handleWifiSetup);
  server.on("/update", HTTP_GET, [](){ sendUpdatePage(); });
  server.on("/update/check", HTTP_POST, handleUpdateCheck);
  server.on("/update/install", HTTP_POST, handleUpdateInstall);
}
