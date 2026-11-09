#pragma once
#include "runtime_policy.h"
namespace emt {
enum class WifiCredentials { Unknown, Missing, Saved };
enum class WifiAction { None, ConnectSaved, ReadCredentials, OpenPortal };
struct WifiRecovery {
  RetrySchedule retry;
  static constexpr uint32_t portalAfterMs = 60000;
  bool offline = false;
  uint32_t offlineSince = 0;
  void portalClosed(uint32_t now) {
    offline = true; offlineSince = now; retry.reset();
  }
  WifiAction step(uint32_t now, WifiCredentials credentials, bool connected,
                  bool portalActive, bool manualPortal) {
    if (portalActive) return WifiAction::None;
    if (manualPortal) { retry.success(now, 30000); return WifiAction::OpenPortal; }
    if (connected) { offline = false; retry.reset(); return WifiAction::None; }
    if (!offline) { offline = true; offlineSince = now; }
    // Opening configuration never means the saved credentials have been erased.
    // Boards with only RST need a recovery route that does not require GPIO0.
    if (credentials == WifiCredentials::Saved &&
        static_cast<uint32_t>(now - offlineSince) >= portalAfterMs) {
      offlineSince = now;
      retry.success(now, 30000);
      return WifiAction::OpenPortal;
    }
    if (!retry.ready(now)) return WifiAction::None;
    retry.success(now, 30000);
    if (credentials == WifiCredentials::Saved) return WifiAction::ConnectSaved;
    if (credentials == WifiCredentials::Missing) return WifiAction::OpenPortal;
    return WifiAction::ReadCredentials; // a driver read failure is not erased WiFi
  }
};
struct PortalWindow {
  bool limited = false;
  uint32_t deadline = 0, duration = 0;
  void start(uint32_t now, bool hasSavedCredentials, bool manual) {
    limited = hasSavedCredentials;
    duration = manual ? 180000 : 600000;
    deadline = now + duration;
  }
  bool expired(uint32_t now, bool clientConnected) {
    if (!limited) return false;
    // Do not close the portal while someone is configuring it from their phone.
    if (clientConnected) deadline = now + duration;
    return due(now, deadline);
  }
};
struct HoldButton {
  uint32_t pressedAt=0;
  bool down=false, fired=false;
  bool update(uint32_t now, bool pressed) {
    if (!pressed) { down=false; fired=false; return false; }
    if (!down) { down=true; pressedAt=now; }
    if (!fired && static_cast<uint32_t>(now-pressedAt)>=3000) { fired=true; return true; }
    return false;
  }
};
}
