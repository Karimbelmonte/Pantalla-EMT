#include "secure_client.h"
#include <time.h>
#include <esp_crt_bundle.h>
#include <esp_task_wdt.h>

// Full Mozilla CA bundle embedded by the pinned ESP-IDF 5.5.2 libraries.
extern const uint8_t bundleStart[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t bundleEnd[] asm("_binary_x509_crt_bundle_end");

bool prepareSecureClient(WiFiClientSecure& client) {
  // Certificate dates cannot be checked with an unsynchronised RTC.
  if (time(nullptr) < 1704067200) return false;
  size_t length = bundleEnd - bundleStart;
  if (esp_crt_bundle_set(bundleStart, length) != ESP_OK) return false;
  client.stop();
  client.setCACert(nullptr); // disable insecure mode, without embedding a single CA
  client.setCACertBundle(bundleStart, length);
  client.setHandshakeTimeout(4);
  client.setTimeout(4000);
  esp_task_wdt_reset();
  return true;
}
