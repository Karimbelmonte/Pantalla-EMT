#include "globals.h"
#include "web_server.h"
#include <WiFi.h>

#define VERSION_MAJOR 0
#define VERSION_MINOR 10

extern const int WDT_TIMEOUT_S;
extern const unsigned long SOFT_WDT_MS;
extern unsigned long lastHealthyTickMs;

static bool isInSleepWindow(uint8_t h, uint8_t startH, uint8_t endH) {
  if (startH == endH) return true;
  if (startH > endH) return (h >= startH) || (h < endH);
  return (h >= startH) && (h < endH);
}

static bool computeSecondsUntilWake(uint32_t& outSeconds) {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return false;
  struct tm nowTm = timeinfo;
  time_t nowT = mktime(&nowTm);
  if (nowT == (time_t)-1) return false;
  struct tm wakeTm = nowTm;
  wakeTm.tm_hour = cfg.sleepEnds;
  wakeTm.tm_min = 0;
  wakeTm.tm_sec = 5;
  time_t wakeT = mktime(&wakeTm);
  if (wakeT == (time_t)-1) return false;
  if (difftime(wakeT, nowT) <= 0) {
    wakeTm.tm_mday += 1;
    wakeT = mktime(&wakeTm);
    if (wakeT == (time_t)-1) return false;
  }
  double diff = difftime(wakeT, nowT);
  if (diff < 10) diff = 10;
  if (diff > (double)(7UL * 24UL * 3600UL)) diff = (double)(7UL * 24UL * 3600UL);
  outSeconds = (uint32_t)diff;
  return true;
}

static void sendResponse(int code, const String& msg) {
  server.send(code, contentTypeText, msg);
}

static void handleInfo() {
  unsigned long uptime = millis();
  char sysUptime[64];
  int days = uptime / 86400000UL;
  int hours = (uptime % 86400000UL) / 3600000UL;
  int minutes = ((uptime % 86400000UL) % 3600000UL) / 60000UL;
  snprintf(sysUptime, sizeof(sysUptime), "%d days, %d hrs, %d min", days, hours, minutes);

  String msg;
  msg += "Pantalla EMT\n";
  msg += "Firmware: v" + String(VERSION_MAJOR) + "." + String(VERSION_MINOR) + "\n";
  msg += "Uptime: " + String(sysUptime) + "\n";
  msg += "WiFi: " + String(WiFi.SSID()) + "\n";
  msg += "IP: " + WiFi.localIP().toString() + "\n";
  msg += "RSSI: " + String(WiFi.RSSI()) + " dB\n";
  msg += "State: " + String((int)appState) + "\n";
  msg += "Screen: " + String((uiScreen == UI_BUS) ? "BUS" : "WEATHER") + "\n";
  msg += "StopName: " + cfg.emtStopName + "\n";
  msg += "StopId: " + cfg.emtStopId + "\n";
  msg += "Brightness: " + String(brightness) + "\n";
  msg += "EMT email set: " + String(cfg.emtEmail.isEmpty() ? "NO" : "YES") + "\n";
  msg += "EMT pass set: " + String(cfg.emtPassword.isEmpty() ? "NO" : "YES") + "\n";
  msg += "OpenWeather key set: " + String(cfg.weatherApiKey.isEmpty() ? "NO" : "YES") + "\n";
  msg += "UI BUS ms: " + String(cfg.uiBusMs) + "\n";
  msg += "UI WEATHER ms: " + String(cfg.uiWeatherMs) + "\n";

  msg += "Sleep enabled: " + String(cfg.sleepEnabled ? "YES" : "NO") + "\n";
  msg += "Sleep mode: " + String(cfg.sleepDeep ? "DEEP" : "SOFT") + "\n";
  msg += "Sleep window: " + String(cfg.sleepStarts) + " -> " + String(cfg.sleepEnds) + "\n";
  
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    msg += "Local time: " + String(timeinfo.tm_hour) + ":" + String(timeinfo.tm_min) + ":" + String(timeinfo.tm_sec) + "\n";
    bool inWin = isInSleepWindow((uint8_t)timeinfo.tm_hour, cfg.sleepStarts, cfg.sleepEnds);
    msg += "In sleep window: " + String(inWin ? "YES" : "NO") + "\n";
    uint32_t secs = 0;
    if (inWin && computeSecondsUntilWake(secs)) {
      msg += "Seconds until wake: " + String(secs) + "\n";
    }
  } else {
    msg += "Local time: (NTP unavailable)\n";
  }

  msg += "Weather coords: " + String(weather.hasCoords ? "YES" : "NO") + "\n";
  if (weather.hasCoords) msg += "Lat: " + String(weather.lat, 6) + " Lon: " + String(weather.lon, 6) + "\n";
  msg += "Weather data: " + String(weather.hasData ? "YES" : "NO") + "\n";
  if (!weather.lastError.isEmpty()) msg += "Weather err: " + weather.lastError + "\n";
  msg += "Success: " + String(dataLoadSuccess) + "\n";
  msg += "Failures: " + String(dataLoadFailure) + "\n";
  msg += "Services: " + String(station.numServices) + "\n";
  if (messages.numMessages) msg += "Message: " + String(messages.messages[0]) + "\n";
  if (!emtLastErrorMsg.isEmpty()) msg += "EMT Error: " + emtLastErrorMsg + "\n";

  msg += "WDT timeout (s): " + String(WDT_TIMEOUT_S) + "\n";
  msg += "SoftWDT (ms): " + String(SOFT_WDT_MS) + "\n";
  msg += "Last healthy (ms ago): " + String((lastHealthyTickMs == 0) ? -1 : (long)(millis() - lastHealthyTickMs)) + "\n";

  sendResponse(200, msg);
}

static void handleReboot() {
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
  s += "    <title>Pantalla EMT - Configuración</title>\n";
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
  s += "        <input type=\"number\" name=\"stopId\" value=\"" + cfg.emtStopId + "\">\n";
  s += "      </div>\n";
  s += "      <div class=\"form-group\">\n";
  s += "        <label>Nombre en pantalla (Opcional)</label>\n";
  s += "        <input type=\"text\" name=\"stopName\" value=\"" + cfg.emtStopName + "\">\n";
  s += "      </div>\n";
  s += "    </div>\n";
  s += "    <div class=\"card\">\n";
  s += "      <h3>\n";
  s += "        <svg viewBox=\"0 0 24 24\"><path d=\"M12 1L3 5V11C3 16.55 6.84 21.74 12 23C17.16 21.74 21 16.55 21 11V5L12 1ZM12 11.99H19C18.47 16.11 15.72 19.78 12 20.92V12H5V6.3L12 3.19V11.99Z\"></path></svg>\n";
  s += "        Credenciales MobilityLabs\n";
  s += "      </h3>\n";
  s += "      <div class=\"form-group\">\n";
  s += "        <label>Correo Electrónico</label>\n";
  s += "        <input type=\"text\" name=\"emtEmail\" value=\"" + cfg.emtEmail + "\">\n";
  s += "      </div>\n";
  s += "      <div class=\"form-group\">\n";
  s += "        <label>Contraseña</label>\n";
  s += "        <input type=\"password\" name=\"emtPassword\" value=\"" + cfg.emtPassword + "\">\n";
  s += "      </div>\n";
  s += "    </div>\n";
  s += "    <div class=\"card\">\n";
  s += "      <h3>\n";
  s += "        <svg viewBox=\"0 0 24 24\"><path d=\"M6.76 4.84L5.34 3.43L4.93 3.84C3.12 5.65 2.11 8.05 2.11 10.61C2.11 15.86 6.36 20.11 11.61 20.11C14.17 20.11 16.57 19.1 18.38 17.29L19.79 18.7L21.2 17.29L8.17 4.25L6.76 4.84ZM11.61 18.11C7.47 18.11 4.11 14.75 4.11 10.61C4.11 8.59 4.9 6.75 6.18 5.41L16.81 16.04C15.47 17.32 13.63 18.11 11.61 18.11Z\"></path><path d=\"M19.89 10.61C19.89 8.35 18.98 6.25 17.38 4.65L15.97 6.06C17.17 7.27 17.89 8.88 17.89 10.61C17.89 12.34 17.17 13.95 15.97 15.16L17.38 16.57C18.98 14.97 19.89 12.87 19.89 10.61Z\"></path></svg>\n";
  s += "        Clima & Entorno\n";
  s += "      </h3>\n";
  s += "      <div class=\"form-group\">\n";
  s += "        <label>OpenWeather API Key</label>\n";
  s += "        <input type=\"text\" name=\"weatherKey\" value=\"" + cfg.weatherApiKey + "\">\n";
  s += "      </div>\n";
  s += "    </div>\n";
  s += "    <div class=\"card\">\n";
  s += "      <h3>\n";
  s += "        <svg viewBox=\"0 0 24 24\"><path d=\"M21 3H3C1.9 3 1 3.9 1 5V19C1 20.1 1.9 21 3 21H21C22.1 21 23 20.1 23 19V5C23 3.9 22.1 3 21 3ZM21 19H3V5H21V19Z\"></path></svg>\n";
  s += "        OLED & Tiempos\n";
  s += "      </h3>\n";
  s += "      <div class=\"form-group\">\n";
  s += "        <label>Brillo del panel (1-255)</label>\n";
  s += "        <input type=\"number\" name=\"brightness\" value=\"" + String(brightness) + "\">\n";
  s += "      </div>\n";
  s += "      <div style=\"display:flex; gap:16px;\">\n";
  s += "        <div class=\"form-group\" style=\"flex:1;\">\n";
  s += "          <label>Buses (seg)</label>\n";
  s += "          <input type=\"number\" name=\"busSecs\" value=\"" + String(cfg.uiBusMs / 1000UL) + "\">\n";
  s += "        </div>\n";
  s += "        <div class=\"form-group\" style=\"flex:1;\">\n";
  s += "          <label>Tiempo (seg)</label>\n";
  s += "          <input type=\"number\" name=\"weatherSecs\" value=\"" + String(cfg.uiWeatherMs / 1000UL) + "\">\n";
  s += "        </div>\n";
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
  s += "    <button type=\"submit\" class=\"btn-submit\">Guardar Configuración 🌿</button>\n";
  s += "  </form>\n";
  s += "  <div class=\"card\" style=\"margin-top:24px; text-align:center;\">\n";
  s += "    <h3 style=\"margin-bottom:12px; justify-content:center; border-bottom:none;\">Acciones del Sistema</h3>\n";
  s += "    <p style=\"font-size:0.9rem; color:var(--text-muted); margin-bottom:20px;\">Controles avanzados para el diagnóstico y estado de la pantalla.</p>\n";
  s += "    <div class=\"footer-links\" style=\"flex-direction:column; gap:12px; margin-top:0;\">\n";
  s += "      <a href=\"/info\" style=\"display:flex; flex-direction:column; padding:12px;\">\n";
  s += "        <span style=\"font-size:1.1rem;\">/info Sistema</span>\n";
  s += "        <span style=\"font-size:0.8rem; font-weight:normal; opacity:0.8; margin-top:4px;\">Muestra el estado interno, memoria RAM, versión y conexión WiFi.</span>\n";
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
  bool changed = false;
  bool needRefreshNow = false;
  bool stopChanged = false;
  bool uiChanged = false;
  bool sleepChanged = false;

  if (server.hasArg("stopId")) {
    String newStop = server.arg("stopId");
    newStop.trim();
    if (newStop != cfg.emtStopId) {
      cfg.emtStopId = newStop;
      clearBoard();
      nextDataUpdate = 0;
      changed = true;
      needRefreshNow = true;
      stopChanged = true;
    }
  }

  if (server.hasArg("stopName")) {
    String newName = server.arg("stopName");
    newName.trim();
    if (newName.isEmpty()) newName = "EMT Madrid";
    if (newName != cfg.emtStopName) {
      cfg.emtStopName = newName;
      changed = true;
    }
  }

  if (server.hasArg("emtEmail")) {
    String v = server.arg("emtEmail");
    v.trim();
    if (v != cfg.emtEmail) {
      cfg.emtEmail = v;
      changed = true;
      needRefreshNow = true;
      emtAccessToken = "";
    }
  }

  if (server.hasArg("emtPassword")) {
    String v = server.arg("emtPassword");
    v.trim();
    if (v != cfg.emtPassword) {
      cfg.emtPassword = v;
      changed = true;
      needRefreshNow = true;
      emtAccessToken = "";
    }
  }

  if (server.hasArg("weatherKey")) {
    String v = server.arg("weatherKey");
    v.trim();
    if (v != cfg.weatherApiKey) {
      cfg.weatherApiKey = v;
      changed = true;
      weather.nextFetchMs = 0;
      weather.hasData = false;
      needRefreshNow = true;
    }
  }

  if (server.hasArg("brightness")) {
    int b = server.arg("brightness").toInt();
    if (b < 1) b = 1;
    if (b > 255) b = 255;
    if (b != brightness) {
      brightness = b;
      u8g2.setContrast(brightness);
      changed = true;
    }
  }

  if (server.hasArg("busSecs")) {
    int v = server.arg("busSecs").toInt();
    if (v < 5) v = 5;
    if (v > 300) v = 300;
    uint32_t ms = (uint32_t)v * 1000UL;
    if (ms != cfg.uiBusMs) {
      cfg.uiBusMs = ms;
      uiChanged = true;
      changed = true;
    }
  }

  if (server.hasArg("weatherSecs")) {
    int v = server.arg("weatherSecs").toInt();
    if (v < 3) v = 3;
    if (v > 120) v = 120;
    uint32_t ms = (uint32_t)v * 1000UL;
    if (ms != cfg.uiWeatherMs) {
      cfg.uiWeatherMs = ms;
      uiChanged = true;
      changed = true;
    }
  }

  bool newSleepEnabled = server.hasArg("sleepEnabled");
  if (newSleepEnabled != cfg.sleepEnabled) {
    cfg.sleepEnabled = newSleepEnabled;
    changed = true;
    sleepChanged = true;
  }

  if (server.hasArg("sleepMode")) {
    String m = server.arg("sleepMode");
    m.trim();
    bool deep = (m == "deep");
    if (deep != cfg.sleepDeep) {
      cfg.sleepDeep = deep;
      changed = true;
      sleepChanged = true;
    }
  }

  if (server.hasArg("sleepStarts")) {
    int v = server.arg("sleepStarts").toInt();
    if (v < 0) v = 0;
    if (v > 23) v = 23;
    if ((uint8_t)v != cfg.sleepStarts) {
      cfg.sleepStarts = (uint8_t)v;
      changed = true;
      sleepChanged = true;
    }
  }

  if (server.hasArg("sleepEnds")) {
    int v = server.arg("sleepEnds").toInt();
    if (v < 0) v = 0;
    if (v > 23) v = 23;
    if ((uint8_t)v != cfg.sleepEnds) {
      cfg.sleepEnds = (uint8_t)v;
      changed = true;
      sleepChanged = true;
    }
  }

  if (stopChanged) weatherResetOnStopChange();
  if (changed) saveConfig(cfg);

  appState = haveStop(cfg) ? ST_RUNNING : ST_NEED_STOP;

  if (uiChanged) uiScheduleNext();

  if (needRefreshNow) {
    if (wifiConnected && appState == ST_RUNNING && haveStop(cfg) && haveCreds(cfg)) {
      (void)getEmtBoard();
    }
  }

  String s = htmlHeader("OK");
  s += "<body style=\"margin:40px; text-align:center; font-family:sans-serif;\">";
  s += "<h2>Guardado</h2><p>Cambios aplicados correctamente.</p>";
  if (sleepChanged && cfg.sleepEnabled && cfg.sleepDeep) {
    s += "<p><b>Nota:</b> Si ahora mismo estás dentro de la ventana de sleep, el dispositivo entrará en deep sleep en pocos segundos.</p>";
  }
  s += "<p><a href=\"/\">Volver al inicio</a></p>";
  s += htmlFooter();
  server.send(200, contentTypeHtml, s);

  markHealthy();
}

static void handleRoot() { handleConfigGet(); }

void setupWebServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/info", HTTP_GET, handleInfo);
  server.on("/reboot", HTTP_GET, handleReboot);
  server.on("/brightness", HTTP_GET, handleBrightness);
  server.on("/sleepnow", HTTP_GET, handleSleepNow);
  server.on("/config", HTTP_GET, handleConfigGet);
  server.on("/config", HTTP_POST, handleConfigPost);
}
