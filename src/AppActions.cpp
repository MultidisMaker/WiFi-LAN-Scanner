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

void applyAppAction(AppAction action, AppView& view, ScannerController& scanner, const AppHooks* hooks) {
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
      break;
    case AppAction::Back:
      view.page = 0;
      if (view.showingHosts) {
        view.showingHosts = false;
      } else if (wifi != nullptr) {
        call(wifi->cancelPassword, wifi->context);
      }
      break;
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
      break;
    case AppAction::KeyboardPage:
      view.keyboardPage ^= 1;
      break;
    case AppAction::Backspace:
      if (wifi != nullptr) {
        call(wifi->backspace, wifi->context);
      }
      break;
    case AppAction::SubmitPassword:
      if (wifi != nullptr) {
        call(wifi->submitPassword, wifi->context);
      }
      break;
    case AppAction::CancelPassword:
      if (wifi != nullptr) {
        call(wifi->cancelPassword, wifi->context);
      }
      break;
    case AppAction::SelectRow:
      if (!view.showingHosts && view.resultsOpen && view.rowOffset >= 0 && view.rowOffset < 6 && wifi != nullptr &&
          wifi->selectResult != nullptr) {
        wifi->selectResult(wifi->context, view.page * 6 + view.rowOffset);
      }
      break;
    case AppAction::None:
      break;
  }
}

void fillAppState(AppState& out, const AppView& view, const ScannerController& scanner, const AppWifiView& wifi) {
  out = AppState();
  if (view.showingHosts) {
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
