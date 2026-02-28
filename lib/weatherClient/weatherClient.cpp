#include <weatherClient.h>
#include <WiFiClientSecure.h>

weatherClient::weatherClient() {}

bool weatherClient::updateWeather(const String& apiKey, const String& lat, const String& lon, const String& lang) {
  lastErrorMsg = "";
  currentWeather = "";
  outTempC = NAN;
  outWindKmh = NAN;
  outDescription = "";

  description = "";
  temperatureC = NAN;
  windMs = NAN;
  weatherItem = 0;

  JsonStreamingParser parser;
  parser.setListener(this);

  WiFiClientSecure httpClient;
  httpClient.setInsecure(); // evita problemas con CA/certificados en ESP32

  int retryCounter = 0;
  while (!httpClient.connect(apiHost, 443) && (retryCounter++ < 15)) {
    delay(200);
  }
  if (retryCounter >= 15) {
    lastErrorMsg = F("Connection timeout");
    return false;
  }

  // OpenWeather current weather endpoint (HTTPS)
  // units=metric, lang=es por defecto (puedes pasar "en", etc.)
  String path = "/data/2.5/weather?units=metric&lang=" + lang +
                "&lat=" + lat + "&lon=" + lon + "&appid=" + apiKey;

  // HTTP/1.1 request
  String request =
    String("GET ") + path + " HTTP/1.1\r\n" +
    "Host: " + apiHost + "\r\n" +
    "User-Agent: ESP32\r\n" +
    "Connection: close\r\n\r\n";

  httpClient.print(request);

  // Espera respuesta (hasta ~8s)
  retryCounter = 0;
  while (!httpClient.available() && retryCounter++ < 40) {
    delay(200);
  }

  if (!httpClient.available()) {
    httpClient.stop();
    lastErrorMsg = F("Response timeout");
    return false;
  }

  // Parse status line
  String statusLine = httpClient.readStringUntil('\n');
  statusLine.trim();

  if (!statusLine.startsWith(F("HTTP/")) || statusLine.indexOf(F("200")) == -1) {
    httpClient.stop();

    if (statusLine.indexOf(F("401")) >= 0)      lastErrorMsg = F("Not Authorized");
    else if (statusLine.indexOf(F("429")) >= 0) lastErrorMsg = F("Rate limited");
    else if (statusLine.indexOf(F("500")) >= 0) lastErrorMsg = F("Server Error");
    else lastErrorMsg = statusLine;

    return false;
  }

  // Skip headers
  while (httpClient.connected() || httpClient.available()) {
    String line = httpClient.readStringUntil('\n');
    if (line == "\r" || line.length() == 1) break;
  }

  bool isBody = false;
  unsigned long dataTimeout = millis() + 10000UL;

  while ((httpClient.available() || httpClient.connected()) && (millis() < dataTimeout)) {
    while (httpClient.available()) {
      char c = (char)httpClient.read();
      if (c == '{' || c == '[') isBody = true;
      if (isBody) parser.parse(c);
    }
    delay(5);
  }

  httpClient.stop();

  if (millis() >= dataTimeout) {
    lastErrorMsg = F("Data timeout");
    return false;
  }

  // Construye salida
  outTempC = temperatureC;
  outWindKmh = isnan(windMs) ? NAN : (windMs * 3.6f);
  outDescription = description;

  // Texto compacto para pantalla
  // Ej: "cielo claro 12° Viento 9km/h"
  int t = isnan(outTempC) ? 0 : (int)lround(outTempC);
  int w = isnan(outWindKmh) ? 0 : (int)lround(outWindKmh);

  if (description.isEmpty()) description = F("tiempo");

  currentWeather = description + " " + String(t) + "\xB0" + " Viento " + String(w) + "km/h";
  return true;
}

// ---- JsonListener ----
void weatherClient::whitespace(char c) { (void)c; }
void weatherClient::startDocument() {}
void weatherClient::key(String key) { currentKey = key; }

void weatherClient::value(String value) {
  // weather[0].description
  if (currentObject == "weather" && weatherItem == 0) {
    if (currentKey == "description") description = value;
  }
  // main.temp
  else if (currentKey == "temp") {
    temperatureC = value.toFloat();
  }
  // wind.speed (m/s)
  else if (currentKey == "speed") {
    windMs = value.toFloat();
  }
}

void weatherClient::endArray() {}
void weatherClient::endObject() {
  if (currentObject == "weather") weatherItem++;
  currentObject = "";
}
void weatherClient::endDocument() {}
void weatherClient::startArray() {}
void weatherClient::startObject() { currentObject = currentKey; }

