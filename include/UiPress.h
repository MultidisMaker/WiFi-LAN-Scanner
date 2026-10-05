#pragma once

#include <stddef.h>
#include <stdint.h>

// Pressed is the momentary inversion. Latched is the persistent Shift
// appearance. LatchedPressed is a Shift control that is also under a finger.
enum class ControlFace : uint8_t { Normal, Pressed, Latched, LatchedPressed };

ControlFace controlFace(bool latched, bool pressed);

// Explicit ASCII case. ctype toupper is not used: the ESP32 nano libc can
// leave it as a no-op, which kept alphabet labels lowercase on device.
char keyGlyph(char base, bool shift);

void maskPassword(char* dest, size_t destLen, int length);

struct PressStep {
  bool began;
  int beganId;
  bool fire;
  int fireId;
  bool cancelled;
  int cancelId;
  bool visualChanged;
  int shownId;
};

// Single-finger press, release, drag-off cancel, and a short visible hold
// after a tap that would otherwise flash past. Time is milliseconds.
class PressTracker {
 public:
  static constexpr uint32_t kAckMs = 120;

  PressStep update(bool down, int hitId, uint32_t nowMs);
  int shownId() const { return shownId_; }

 private:
  bool wasDown_ = false;
  bool inside_ = false;
  int activeId_ = -1;
  int ackId_ = -1;
  uint32_t downMs_ = 0;
  uint32_t ackUntilMs_ = 0;
  int shownId_ = -1;
};

bool pressTrackerSelfTest();
bool keyGlyphSelfTest();
bool maskPasswordSelfTest();
