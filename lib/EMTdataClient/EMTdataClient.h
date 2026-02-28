#pragma once
#include <Arduino.h>
#include <stationData.h>

#ifndef UPD_SUCCESS
#define UPD_SUCCESS 0
#define UPD_NO_CHANGE 1
#define UPD_DATA_ERROR 2
#define UPD_UNAUTHORISED 3
#define UPD_HTTP_ERROR 4
#define UPD_INCOMPLETE 5
#define UPD_NO_RESPONSE 6
#define UPD_TIMEOUT 7
#endif

class EMTdataClient {
public:
  String lastErrorMsg = "";

  // ahora pasamos stopId + clientId + apiKey (los dos de EMT)
  int updateArrivals(rdStation* station, stnMessages* messages,
                     const String& stopId,
                     const String& clientId,
                     const String& apiKey,
                     void (*callback)() = nullptr);
};

