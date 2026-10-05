#pragma once

#include <Arduino.h>

#include "BoardConfig.h"

enum class WifiPhase : uint8_t {
  Idle,
  Scanning,
  Results,
  Password,
  Connecting,
  Connected,
  Failed
};

struct WifiAp {
  char ssid[kMaxSsidLen + 1];
  int32_t rssi;
  bool secure;
};

class WifiService {
 public:
  void begin();
  void loop();
  WifiPhase phase() const;
  bool hasSavedNetwork() const;
  const char* savedSsid() const;
  const char* statusText() const;
  int resultCount() const;
  const WifiAp* resultAt(int index) const;
  void requestScan();
  void selectResult(int index);
  void cancelPassword();
  bool shiftOn() const;
  void toggleShift();
  void typeChar(char c);
  void backspace();
  int passwordLength() const;
  void submitPassword();
  void forget();
  const char* selectedSsid() const;
  // Types one synthetic character, toggles Shift, and checks the buffer is unchanged.
  // Clears the buffer before returning. Does not print the character.
  bool maskingSelfTest();

 private:
  WifiPhase phase_ = WifiPhase::Idle;
  bool saved_ = false;
  bool shift_ = false;
  bool selectedSecure_ = false;
  int passwordLength_ = 0;
  unsigned long connectStartedMs_ = 0;
  int resultCount_ = 0;
  char savedSsid_[kMaxSsidLen + 1] = {};
  char savedPsk_[kMaxPassLen + 1] = {};
  char selectedSsid_[kMaxSsidLen + 1] = {};
  char typedPsk_[kMaxPassLen + 1] = {};
  char status_[48] = "Wi-Fi idle";
  WifiAp results_[kMaxScanResults] = {};

  void setStatus(const char* text);
  void loadSaved();
  void storeSaved(const char* ssid, const char* psk);
  void clearSaved();
  void beginConnect(const char* ssid, const char* psk);
  void collectScan();
};
