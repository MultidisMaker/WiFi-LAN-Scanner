#include "AppActions.h"

#include <stdio.h>
#include <string.h>

#include "NetMath.h"

namespace {

void copyToken(char* dest, size_t cap, const char* text) {
  size_t n = 0;
  if (dest == nullptr || cap == 0) {
    return;
  }
  if (text != nullptr) {
    for (size_t i = 0; text[i] != '\0' && n + 1 < cap; ++i) {
      const unsigned char c = static_cast<unsigned char>(text[i]);
      if (c < 32 || c == 127) {
        continue;
      }
      dest[n++] = static_cast<char>(c);
    }
  }
  dest[n] = '\0';
}

const char* screenName(AppScreen screen) {
  switch (screen) {
    case AppScreen::Results:
      return "results";
    case AppScreen::Entry:
      return "entry";
    case AppScreen::Hosts:
      return "hosts";
    case AppScreen::Settings:
      return "settings";
    case AppScreen::Home:
      return "home";
  }
  return "home";
}

const char* publicPhase(const char* phase) {
  if (phase == nullptr || phase[0] == '\0') {
    return "idle";
  }
  if (strcmp(phase, "password") == 0 || strcmp(phase, "Password") == 0) {
    return "entry";
  }
  return phase;
}

void call(void (*hook)(void*), void* context) {
  if (hook != nullptr) {
    hook(context);
  }
}

}  // namespace

AppAction actionFromControl(int id, int* rowOffset) {
  if (rowOffset != nullptr) {
    *rowOffset = -1;
  }
  if (id >= IdRow0 && id < IdRow0 + 6) {
    if (rowOffset != nullptr) {
      *rowOffset = id - IdRow0;
    }
    return AppAction::SelectRow;
  }
  switch (id) {
    case IdFind:
      return AppAction::FindNetworks;
    case IdForget:
      return AppAction::ForgetNetwork;
    case IdStart:
      return AppAction::StartScan;
    case IdPause:
      return AppAction::PauseScan;
    case IdResume:
      return AppAction::ResumeScan;
    case IdStop:
      return AppAction::StopScan;
    case IdReset:
      return AppAction::ResetScan;
    case IdHosts:
      return AppAction::OpenHosts;
    case IdSettings:
      return AppAction::OpenSettings;
    case IdProfileBasic:
      if (rowOffset != nullptr) {
        *rowOffset = 0;
      }
      return AppAction::SetProfile;
    case IdProfileCommon:
      if (rowOffset != nullptr) {
        *rowOffset = 1;
      }
      return AppAction::SetProfile;
    case IdProfileDetailed:
      if (rowOffset != nullptr) {
        *rowOffset = 2;
      }
      return AppAction::SetProfile;
    case IdOpenService:
      return AppAction::OpenService;
    case IdOpenRange:
      return AppAction::OpenRange;
    case IdRangeAuto:
      return AppAction::SetAutomatic;
    case IdRangeCustom:
      return AppAction::SetCustom;
    case IdCount64:
      if (rowOffset != nullptr) {
        *rowOffset = 64;
      }
      return AppAction::SetLimit;
    case IdCount128:
      if (rowOffset != nullptr) {
        *rowOffset = 128;
      }
      return AppAction::SetLimit;
    case IdCount256:
      if (rowOffset != nullptr) {
        *rowOffset = 256;
      }
      return AppAction::SetLimit;
    case IdWindowPrev:
      return AppAction::WindowPrev;
    case IdWindowNext:
      return AppAction::WindowNext;
    case IdBack:
      return AppAction::Back;
    case IdNext:
      return AppAction::NextPage;
    case IdPrev:
      return AppAction::PrevPage;
    case IdShift:
      return AppAction::Shift;
    case IdPage:
      return AppAction::KeyboardPage;
    case IdDel:
      return AppAction::Backspace;
    case IdOk:
      return AppAction::SubmitPassword;
    case IdClose:
      return AppAction::CancelPassword;
    default:
      return AppAction::None;
  }
}

bool applyAppAction(AppAction action, AppView& view, ScannerController& scanner, const AppHooks* hooks,
                    const char* text) {
  const AppHooks* wifi = hooks;
  switch (action) {
    case AppAction::FindNetworks:
      view.page = 0;
      if (wifi != nullptr) {
        call(wifi->findNetworks, wifi->context);
      }
      break;
    case AppAction::ForgetNetwork:
      if (wifi != nullptr) {
        call(wifi->forgetNetwork, wifi->context);
      }
      break;
    case AppAction::StartScan:
      scanner.start();
      break;
    case AppAction::PauseScan:
      scanner.pause();
      break;
    case AppAction::ResumeScan:
      scanner.resume();
      break;
    case AppAction::StopScan:
      scanner.stop();
      break;
    case AppAction::ResetScan:
      view.page = 0;
      scanner.reset();
      break;
    case AppAction::OpenHosts:
      view.page = 0;
      view.showingHosts = true;
      view.showingSettings = false;
      break;
    case AppAction::OpenSettings:
      if (!view.resultsOpen && !view.entryOpen) {
        view.page = 0;
        view.showingHosts = false;
        view.showingSettings = true;
        view.settingsPage = SettingsPage::Menu;
      }
      return true;
    case AppAction::OpenService:
      if (!view.resultsOpen && !view.entryOpen) {
        view.page = 0;
        view.showingHosts = false;
        view.showingSettings = true;
        view.settingsPage = SettingsPage::Service;
      }
      return true;
    case AppAction::OpenRange:
      if (!view.resultsOpen && !view.entryOpen) {
        view.page = 0;
        view.showingHosts = false;
        view.showingSettings = true;
        view.settingsPage = SettingsPage::Range;
      }
      return true;
    case AppAction::SetAutomatic:
      return scanner.setAutomatic();
    case AppAction::SetCustom:
      if (text == nullptr || text[0] == '\0') {
        if (view.resultsOpen || view.entryOpen) {
          return false;
        }
        view.page = 0;
        view.showingHosts = false;
        view.showingSettings = true;
        view.settingsPage = SettingsPage::Edit;
        return true;
      }
      {
        Ipv4 start;
        if (!parseIpv4(text, start) || !scanner.setCustomStart(start)) {
          return false;
        }
        view.showingHosts = false;
        view.showingSettings = true;
        view.settingsPage = SettingsPage::Range;
        return true;
      }
    case AppAction::SetLimit:
      return scanner.setLimit(static_cast<uint16_t>(view.rowOffset));
    case AppAction::WindowNext:
      return scanner.windowNext();
    case AppAction::WindowPrev:
      return scanner.windowPrev();
    case AppAction::SetProfile:
      if (view.rowOffset >= 0 && view.rowOffset <= 2) {
        view.profile = static_cast<ServiceProfile>(view.rowOffset);
        if (wifi != nullptr && wifi->setProfile != nullptr) {
          wifi->setProfile(wifi->context, view.rowOffset);
        }
      }
      return true;
    case AppAction::Back:
      view.page = 0;
      if (view.showingSettings) {
        if (view.settingsPage == SettingsPage::Edit) {
          view.settingsPage = SettingsPage::Range;
        } else if (view.settingsPage != SettingsPage::Menu) {
          view.settingsPage = SettingsPage::Menu;
        } else {
          view.showingSettings = false;
        }
      } else if (view.showingHosts) {
        view.showingHosts = false;
      } else if (view.resultsOpen) {
        if (wifi != nullptr) {
          call(wifi->closeResults, wifi->context);
        }
      } else if (wifi != nullptr) {
        call(wifi->cancelPassword, wifi->context);
      }
      return true;
    case AppAction::NextPage:
      if (view.showingHosts) {
        if ((view.page + 1) * 6 < static_cast<int>(view.observedCount)) {
          ++view.page;
        }
      } else if ((view.page + 1) * 6 < view.resultCount) {
        ++view.page;
      }
      break;
    case AppAction::PrevPage:
      if (view.page > 0) {
        --view.page;
      }
      break;
    case AppAction::Shift:
      if (wifi != nullptr) {
        call(wifi->toggleShift, wifi->context);
      }
      return true;
    case AppAction::KeyboardPage:
      view.keyboardPage ^= 1;
      return true;
    case AppAction::Backspace:
      if (wifi != nullptr) {
        call(wifi->backspace, wifi->context);
      }
      return true;
    case AppAction::SubmitPassword:
      if (wifi != nullptr) {
        call(wifi->submitPassword, wifi->context);
      }
      return true;
    case AppAction::CancelPassword:
      if (wifi != nullptr) {
        call(wifi->cancelPassword, wifi->context);
      }
      return true;
    case AppAction::SelectRow:
      if (!view.showingHosts && !view.showingSettings && view.resultsOpen && view.rowOffset >= 0 && view.rowOffset < 6 &&
          wifi != nullptr && wifi->selectResult != nullptr) {
        wifi->selectResult(wifi->context, view.page * 6 + view.rowOffset);
      }
      return true;
    case AppAction::None:
      return true;
  }
  return true;
}

void fillAppState(AppState& out, const AppView& view, const ScannerController& scanner, const AppWifiView& wifi,
                  ServiceProfile profile) {
  out = AppState();
  if (view.showingSettings && !view.showingHosts && !view.entryOpen && !wifi.entry && !wifi.results && !view.resultsOpen) {
    out.screen = AppScreen::Settings;
  } else if (view.showingHosts) {
    out.screen = AppScreen::Hosts;
  } else if (wifi.entry) {
    out.screen = AppScreen::Entry;
  } else if (wifi.results) {
    out.screen = AppScreen::Results;
  } else {
    out.screen = AppScreen::Home;
  }
  copyToken(out.wifiPhase, sizeof(out.wifiPhase), publicPhase(wifi.phase));
  copyToken(out.ssid, sizeof(out.ssid), wifi.ssid);
  out.saved = wifi.saved;
  copyToken(out.scan, sizeof(out.scan), scanStateName(scanner.state()));
  out.processed = scanner.processedCount();
  out.candidates = scanner.candidateCount();
  out.observed = scanner.observedCount();
  if (scanner.hasCurrent()) {
    formatIpv4(scanner.currentAddress(), out.current, sizeof(out.current));
  }
  if (scanner.hasLast()) {
    formatIpv4(scanner.lastAddress(), out.last, sizeof(out.last));
  }
  const ObservedHost* newest = scanner.newest();
  if (newest != nullptr) {
    formatIpv4(newest->ip, out.newest, sizeof(out.newest));
  }
  out.elapsedMs = scanner.elapsedMs();
  out.hostsOpen = view.showingHosts;
  out.page = view.page;
  out.keyboardPage = view.keyboardPage;
  out.shift = wifi.shift;
  const ScanState state = scanner.state();
  out.canStart = state == ScanState::Idle || state == ScanState::Complete;
  out.canPause = state == ScanState::Scanning;
  out.canResume = state == ScanState::Paused;
  copyToken(out.profile, sizeof(out.profile), serviceProfileToken(profile));
  const RangePreview range = scanner.preview();
  copyToken(out.rangeMode, sizeof(out.rangeMode), range.mode == RangeMode::Custom ? "custom" : "automatic");
  if (range.valid) {
    formatIpv4(range.start, out.rangeStart, sizeof(out.rangeStart));
    formatIpv4(range.end, out.rangeEnd, sizeof(out.rangeEnd));
  }
  out.rangeLimit = addressLimitOk(range.limit) ? range.limit : 256;
}

int formatAppStateLine(char* out, int cap, const AppState& state) {
  if (out == nullptr || cap < 16) {
    return -1;
  }
  const int n = snprintf(
      out, static_cast<size_t>(cap),
      "WLS state screen=%s wifi=%s ssid=%s saved=%d scan=%s processed=%u candidates=%u observed=%u current=%s last=%s "
      "newest=%s elapsed=%lu hosts=%d page=%d keys=%d shift=%d canStart=%d canPause=%d canResume=%d",
      screenName(state.screen), state.wifiPhase, state.ssid, state.saved ? 1 : 0, state.scan, state.processed,
      state.candidates, state.observed, state.current, state.last, state.newest, static_cast<unsigned long>(state.elapsedMs),
      state.hostsOpen ? 1 : 0, state.page, state.keyboardPage, state.shift ? 1 : 0, state.canStart ? 1 : 0,
      state.canPause ? 1 : 0, state.canResume ? 1 : 0);
  if (n < 0 || n >= cap) {
    out[0] = '\0';
    return -1;
  }
  return n;
}
