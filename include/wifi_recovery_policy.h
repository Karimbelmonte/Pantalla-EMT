#pragma once
#include "runtime_policy.h"
namespace emt {
enum class WifiCredentials { Unknown, Missing, Saved };
enum class WifiAction { None, ConnectSaved, ReadCredentials, OpenPortal };
struct WifiRecovery {
  RetrySchedule retry;
  WifiAction step(uint32_t now, WifiCredentials credentials, bool connected,
                  bool portalActive, bool manualPortal) {
    if (portalActive) return WifiAction::None;
    if (manualPortal) { retry.success(now, 30000); return WifiAction::OpenPortal; }
    if (connected) { retry.reset(); return WifiAction::None; }
    if (!retry.ready(now)) return WifiAction::None;
    retry.success(now, 30000);
    if (credentials == WifiCredentials::Saved) return WifiAction::ConnectSaved;
    if (credentials == WifiCredentials::Missing) return WifiAction::OpenPortal;
    return WifiAction::ReadCredentials; // a driver read failure is not erased WiFi
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
