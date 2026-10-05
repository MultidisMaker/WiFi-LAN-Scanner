#include "WifiService.h"

#include <Preferences.h>
#include <WiFi.h>

namespace {
void copyText(char* dest, size_t destLen, const char* src) {
  if (destLen == 0) {
    return;
  }
  size_t i = 0;
  if (src != nullptr) {
    for (; src[i] != '\0' && i + 1 < destLen; ++i) {
      dest[i] = src[i];
    }
  }
  dest[i] = '\0';
}
}

void WifiService::setStatus(const char* text) { copyText(status_, sizeof(status_), text); }

void WifiService::begin() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.disconnect(false, false);
  loadSaved();
  if (saved_ && savedSsid_[0] != '\0') {
    beginConnect(savedSsid_, savedPsk_);
    setStatus("Reconnecting saved network");
  } else {
    phase_ = WifiPhase::Idle;
    setStatus("No saved network");
  }
  Serial.printf("WLS wifi saved=%s\n", saved_ ? "yes" : "no");
}

void WifiService::loadSaved() {
  Preferences prefs;
  prefs.begin("wlan", false);
  if (prefs.isKey("ssid")) {
    prefs.getString("ssid", savedSsid_, sizeof(savedSsid_));
  }
  if (prefs.isKey("psk")) {
    prefs.getString("psk", savedPsk_, sizeof(savedPsk_));
  }
  prefs.end();
  saved_ = savedSsid_[0] != '\0';
}

void WifiService::storeSaved(const char* ssid, const char* psk) {
  Preferences prefs;
  prefs.begin("wlan", false);
  prefs.putString("ssid", ssid);
  prefs.putString("psk", psk == nullptr ? "" : psk);
  prefs.end();
  copyText(savedSsid_, sizeof(savedSsid_), ssid);
  copyText(savedPsk_, sizeof(savedPsk_), psk == nullptr ? "" : psk);
  saved_ = true;
}

void WifiService::clearSaved() {
  Preferences prefs;
  prefs.begin("wlan", false);
  prefs.remove("ssid");
  prefs.remove("psk");
  prefs.end();
  memset(savedSsid_, 0, sizeof(savedSsid_));
  memset(savedPsk_, 0, sizeof(savedPsk_));
  memset(typedPsk_, 0, sizeof(typedPsk_));
  passwordLength_ = 0;
  saved_ = false;
}

void WifiService::beginConnect(const char* ssid, const char* psk) {
  WiFi.disconnect(false, false);
  WiFi.begin(ssid, psk == nullptr ? "" : psk);
  connectStartedMs_ = millis();
  phase_ = WifiPhase::Connecting;
}

void WifiService::loop() {
  if (phase_ == WifiPhase::Scanning) {
    collectScan();
  }
  if (phase_ == WifiPhase::Connecting) {
    if (WiFi.status() == WL_CONNECTED) {
      phase_ = WifiPhase::Connected;
      setStatus("Connected");
      Serial.println("WLS wifi connected");
    } else if (millis() - connectStartedMs_ >= kWifiConnectTimeoutMs) {
      phase_ = WifiPhase::Failed;
      setStatus("Connection failed");
      Serial.println("WLS wifi connect-failed");
    }
  } else if (phase_ == WifiPhase::Connected && WiFi.status() != WL_CONNECTED) {
    phase_ = WifiPhase::Connecting;
    connectStartedMs_ = millis();
    setStatus("Reconnecting");
  }
}

WifiPhase WifiService::phase() const { return phase_; }

bool WifiService::hasSavedNetwork() const { return saved_; }

const char* WifiService::savedSsid() const { return savedSsid_; }

const char* WifiService::statusText() const { return status_; }

int WifiService::resultCount() const { return resultCount_; }

const WifiAp* WifiService::resultAt(int index) const {
  if (index < 0 || index >= resultCount_) {
    return nullptr;
  }
  return &results_[index];
}

void WifiService::requestScan() {
  resultCount_ = 0;
  WiFi.scanDelete();
  WiFi.mode(WIFI_STA);
  if (WiFi.scanNetworks(true) == WIFI_SCAN_FAILED) {
    phase_ = WifiPhase::Failed;
    setStatus("Scan failed");
    Serial.println("WLS wifi-scan failed");
    return;
  }
  phase_ = WifiPhase::Scanning;
  setStatus("Scanning Wi-Fi");
  Serial.println("WLS wifi-scan started");
}

void WifiService::collectScan() {
  const int found = WiFi.scanComplete();
  if (found == WIFI_SCAN_RUNNING) {
    return;
  }
  if (found < 0) {
    phase_ = WifiPhase::Failed;
    setStatus("Scan failed");
    Serial.println("WLS wifi-scan failed");
    return;
  }
  resultCount_ = 0;
  for (int i = 0; i < found && resultCount_ < kMaxScanResults; ++i) {
    WifiAp& ap = results_[resultCount_];
    memset(&ap, 0, sizeof(ap));
    copyText(ap.ssid, sizeof(ap.ssid), WiFi.SSID(i).c_str());
    if (ap.ssid[0] == '\0') {
      continue;
    }
    ap.rssi = WiFi.RSSI(i);
    ap.secure = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    ++resultCount_;
  }
  WiFi.scanDelete();
  phase_ = WifiPhase::Results;
  setStatus("Select a network");
  Serial.printf("WLS wifi-scan count=%d\n", resultCount_);
}

void WifiService::selectResult(int index) {
  const WifiAp* ap = resultAt(index);
  if (ap == nullptr) {
    return;
  }
  copyText(selectedSsid_, sizeof(selectedSsid_), ap->ssid);
  selectedSecure_ = ap->secure;
  memset(typedPsk_, 0, sizeof(typedPsk_));
  passwordLength_ = 0;
  shift_ = false;
  if (!selectedSecure_) {
    storeSaved(selectedSsid_, "");
    beginConnect(selectedSsid_, "");
    setStatus("Connecting open network");
    return;
  }
  phase_ = WifiPhase::Password;
  setStatus("Enter password");
}

void WifiService::cancelPassword() {
  memset(typedPsk_, 0, sizeof(typedPsk_));
  passwordLength_ = 0;
  phase_ = resultCount_ > 0 ? WifiPhase::Results : WifiPhase::Idle;
  setStatus(phase_ == WifiPhase::Results ? "Select a network" : "No saved network");
}

bool WifiService::shiftOn() const { return shift_; }

void WifiService::toggleShift() { shift_ = !shift_; }

void WifiService::typeChar(char c) {
  if (passwordLength_ >= kMaxPassLen) {
    return;
  }
  typedPsk_[passwordLength_++] = c;
  typedPsk_[passwordLength_] = '\0';
}

void WifiService::backspace() {
  if (passwordLength_ <= 0) {
    return;
  }
  typedPsk_[--passwordLength_] = '\0';
}

int WifiService::passwordLength() const { return passwordLength_; }

void WifiService::submitPassword() {
  storeSaved(selectedSsid_, typedPsk_);
  beginConnect(selectedSsid_, typedPsk_);
  setStatus("Connecting");
  memset(typedPsk_, 0, sizeof(typedPsk_));
  passwordLength_ = 0;
}

bool WifiService::maskingSelfTest() {
  if (passwordLength_ != 0 || shift_ || typedPsk_[0] != '\0') {
    return false;
  }
  typeChar('a');
  const bool typed = passwordLength_ == 1 && typedPsk_[0] == 'a' && typedPsk_[1] == '\0';
  toggleShift();
  const bool preserved = typed && shift_ && passwordLength_ == 1 && typedPsk_[0] == 'a';
  toggleShift();
  const bool restored = preserved && !shift_ && typedPsk_[0] == 'a';
  backspace();
  memset(typedPsk_, 0, sizeof(typedPsk_));
  passwordLength_ = 0;
  shift_ = false;
  return restored && passwordLength_ == 0 && typedPsk_[0] == '\0';
}

void WifiService::forget() {
  clearSaved();
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_STA);
  phase_ = WifiPhase::Idle;
  setStatus("Saved network forgotten");
  Serial.println("WLS wifi forgotten");
}

const char* WifiService::selectedSsid() const { return selectedSsid_; }
