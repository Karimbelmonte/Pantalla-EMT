#pragma once
#include <Arduino.h>
#include <JsonListener.h>
#include <JsonStreamingParser.h>

class weatherClient : public JsonListener {

  private:
    const char* apiHost = "api.openweathermap.org";
    String currentKey = "";
    String currentObject = "";
    int weatherItem = 0;

    // parsed fields
    String description;
    float temperatureC = NAN;
    float windMs = NAN;

  public:
    // outputs
    String currentWeather = "";
    String lastErrorMsg = "";

    // extra fields (útiles si luego quieres dibujar mejor)
    float outTempC = NAN;
    float outWindKmh = NAN;
    String outDescription = "";

    weatherClient();

    // lat/lon como String para que tú pases String(lat, 6)
    bool updateWeather(const String& apiKey, const String& lat, const String& lon, const String& lang = "es");

    // JsonListener
    virtual void whitespace(char c) override;
    virtual void startDocument() override;
    virtual void key(String key) override;
    virtual void value(String value) override;
    virtual void endArray() override;
    virtual void endObject() override;
    virtual void endDocument() override;
    virtual void startArray() override;
    virtual void startObject() override;
};

