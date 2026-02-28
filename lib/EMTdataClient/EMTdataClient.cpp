#include "EMTdataClient.h"

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

static const uint32_t HTTP_TIMEOUT_MS = 12000;

// Intentamos varias versiones porque EMT ha tenido v1/v2/v3.
// Devuelve true si responde HTTP 200 y pone payload.
static bool httpJsonRequest(
  const String& method,              // "GET" o "POST"
  const String& url,
  const std::vector<std::pair<String,String>>& headers,
  const String& body,
  String& payloadOut,
  int& httpCodeOut
) {
  WiFiClientSecure client;
  client.setInsecure(); // simplifica (sin CA). Si quieres, luego lo endurecemos.

  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);

  if (!http.begin(client, url)) {
    httpCodeOut = -1;
    return false;
  }

  for (auto& h : headers) http.addHeader(h.first, h.second);

  int code = -1;
  if (method == "GET") {
    code = http.GET();
  } else {
    // POST
    if (!body.isEmpty()) http.addHeader("Content-Type", "application/json");
    code = http.POST(body);
  }

  httpCodeOut = code;
  if (code > 0) payloadOut = http.getString();
  http.end();

  return (code == 200);
}

// Parseo token: response["data"][0]["accessToken"]
static bool parseAccessToken(const String& json, String& tokenOut, String& errOut) {
  StaticJsonDocument<2048> doc;
  DeserializationError err = deserializeJson(doc, json);
  if (err) {
    errOut = "JSON login inválido";
    return false;
  }

  JsonVariant data = doc["data"];
  if (!data.isNull() && data.is<JsonArray>() && data.as<JsonArray>().size() > 0) {
    JsonObject o = data.as<JsonArray>()[0];
    const char* t = o["accessToken"] | nullptr;
    if (t && String(t).length() > 5) {
      tokenOut = t;
      return true;
    }
  }

  // fallback: algunas APIs lo devuelven directo
  const char* t2 = doc["accessToken"] | nullptr;
  if (t2 && String(t2).length() > 5) {
    tokenOut = t2;
    return true;
  }

  errOut = "No encuentro accessToken";
  return false;
}

static bool parseArrivalsToStationData(const String& json,
                                       rdStation* station,
                                       stnMessages* messages,
                                       String& errOut)
{
  StaticJsonDocument<16384> doc;
  DeserializationError err = deserializeJson(doc, json);
  if (err) {
    errOut = "JSON arrivals inválido";
    return false;
  }

  // Formato clásico: data[0].Arrive = array
  JsonArray data = doc["data"].as<JsonArray>();
  if (data.isNull() || data.size() == 0) {
    errOut = "Respuesta sin data[]";
    return false;
  }

  JsonObject root = data[0].as<JsonObject>();
  JsonArray arr = root["Arrive"].as<JsonArray>();
  if (arr.isNull()) {
    errOut = "Sin Arrive[]";
    return false;
  }

  // Mensajes
  messages->numMessages = 0;
  const char* msg = root["Description"] | nullptr;
  if (msg && strlen(msg) > 0) {
    strncpy(messages->messages[0], msg, MAXCALLINGSIZE);
    messages->messages[0][MAXCALLINGSIZE-1] = '\0';
    messages->numMessages = 1;
  }

  // Servicios (máx 3 para tu UI actual)
  station->numServices = 0;

  int n = 0;
  for (JsonObject a : arr) {
    const char* line = a["line"] | nullptr;
    const char* dest = a["destination"] | nullptr;
    long estimate = a["estimateArrive"] | -1; // en segundos en el formato clásico

    if (!line || !dest || estimate < 0) continue;

    // destination: "27 -> Plaza Castilla"
    char destbuf[MAXLOCATIONSIZE];
    snprintf(destbuf, sizeof(destbuf), "%s      ->      %s", line, dest);

    strncpy(station->service[n].destination, destbuf, MAXLOCATIONSIZE);
    station->service[n].destination[MAXLOCATIONSIZE-1] = '\0';
    station->service[n].timeToStation = (int)estimate;

    n++;
    if (n >= 3) break;
  }

  station->numServices = n;
  station->boardChanged = true;

  if (n == 0) {
    errOut = "Arrive[] vacío o sin campos esperados";
    return false;
  }

  return true;
}

int EMTdataClient::updateArrivals(rdStation* station, stnMessages* messages,
                                  const String& stopId,
                                  const String& clientId,
                                  const String& apiKey,
                                  void (*callback)())
{
  lastErrorMsg = "";

  if (!station || !messages) {
    lastErrorMsg = "station/messages null";
    return UPD_DATA_ERROR;
  }
  if (WiFi.status() != WL_CONNECTED) {
    lastErrorMsg = "Sin WiFi";
    return UPD_NO_RESPONSE;
  }
  if (stopId.isEmpty() || clientId.isEmpty() || apiKey.isEmpty()) {
    lastErrorMsg = "Faltan credenciales/parada";
    return UPD_UNAUTHORISED;
  }

  // 1) LOGIN: sacar accessToken
  String token;
  String payload;
  int code = -1;

  // Variantes posibles (v3/v2/v1) + headers (X-ApiKey o passKey)
  struct LoginAttempt { String url; bool useXApiKey; };
  LoginAttempt attempts[] = {
    { "https://openapi.emtmadrid.es/v3/mobilitylabs/user/login/", true  },
    { "https://openapi.emtmadrid.es/v2/mobilitylabs/user/login/", true  },
    { "https://openapi.emtmadrid.es/v1/mobilitylabs/user/login/", true  },
    { "https://openapi.emtmadrid.es/v3/mobilitylabs/user/login/", false },
    { "https://openapi.emtmadrid.es/v2/mobilitylabs/user/login/", false },
    { "https://openapi.emtmadrid.es/v1/mobilitylabs/user/login/", false }
  };

  bool logged = false;
  String loginErr;

  for (auto &a : attempts) {
    std::vector<std::pair<String,String>> headers;
    headers.push_back({"X-ClientId", clientId});
    headers.push_back({ a.useXApiKey ? "X-ApiKey" : "passKey", apiKey });

    payload = "";
    code = -1;

    // algunas versiones son GET; otras POST. Intentamos GET primero, si no, POST con "{}"
    bool ok = httpJsonRequest("GET", a.url, headers, "", payload, code);
    if (!ok) {
      ok = httpJsonRequest("POST", a.url, headers, "{}", payload, code);
    }
    if (ok) {
      if (parseAccessToken(payload, token, loginErr)) {
        logged = true;
        break;
      }
    }

    if (callback) callback();
  }

  if (!logged) {
    lastErrorMsg = "Login EMT falló (" + String(code) + "): " + loginErr;
    return (code == 401 || code == 403) ? UPD_UNAUTHORISED : UPD_HTTP_ERROR;
  }

  // 2) ARRIVALS
  // Formato clásico:
  // POST https://openapi.emtmadrid.es/v1/transport/busemtmad/stops/{stopId}/arrives/
  // body: {"stopId":"29","Text_EstimationsRequired_YN":"Y"}
  // (También hay variantes v2/v3)
  String urls[] = {
    "https://openapi.emtmadrid.es/v3/transport/busemtmad/stops/" + stopId + "/arrives/",
    "https://openapi.emtmadrid.es/v2/transport/busemtmad/stops/" + stopId + "/arrives/",
    "https://openapi.emtmadrid.es/v1/transport/busemtmad/stops/" + stopId + "/arrives/"
  };

  String body = "{\"stopId\":\"" + stopId + "\",\"Text_EstimationsRequired_YN\":\"Y\"}";

  bool okArr = false;
  String arrJson;
  int arrCode = -1;

  for (auto &u : urls) {
    std::vector<std::pair<String,String>> headers;
    headers.push_back({"accessToken", token});

    arrJson = "";
    arrCode = -1;

    // suele ser POST
    bool ok = httpJsonRequest("POST", u, headers, body, arrJson, arrCode);
    if (ok) {
      String parseErr;
      if (parseArrivalsToStationData(arrJson, station, messages, parseErr)) {
        okArr = true;
        break;
      } else {
        lastErrorMsg = "Parse arrivals: " + parseErr;
      }
    }

    if (callback) callback();
  }

  if (!okArr) {
    if (lastErrorMsg.isEmpty()) lastErrorMsg = "Arrivals falló (" + String(arrCode) + ")";
    return (arrCode == 401 || arrCode == 403) ? UPD_UNAUTHORISED : UPD_HTTP_ERROR;
  }

  return UPD_SUCCESS;
}

