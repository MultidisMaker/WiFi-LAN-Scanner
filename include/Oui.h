#pragma once

#include <stddef.h>
#include <stdint.h>

// Offline MAC classification and MA-L lookup. The lookup reads a caller-supplied
// sorted table. It does not transmit and it does not allocate.
enum class MacClass : uint8_t { Absent = 0, Group = 1, Local = 2, Global = 3 };

enum class OuiState : uint8_t {
  Unset = 0,
  None = 1,
  DataMissing = 2,
  Group = 3,
  Local = 4,
  Unknown = 5,
  Known = 6
};

struct OuiEntry {
  uint32_t prefix24;
  uint32_t nameOffset;
};

struct OuiTable {
  const OuiEntry* entries;
  uint32_t count;
  const char* names;
};

struct OuiResult {
  MacClass macClass = MacClass::Absent;
  OuiState state = OuiState::Unset;
  const char* name = nullptr;
};

// Group bit wins over the local bit. A null MAC is Absent.
MacClass classifyMac(const uint8_t mac[6]);

// Accepts AABBCC, AA:BB:CC, or AA-BB-CC, with surrounding spaces and either hex case.
// Both separators must match. Returns false for any other shape.
bool parseOuiAssignment(const char* text, uint32_t* prefix24);

// Reads at most 128 bytes. Keeps letters, digits, space, and . _ - & ' ( ) , /
// Stores at most 64, then strips leading spaces and trailing spaces or dots.
// Returns false when nothing remains.
bool sanitizeManufacturer(const char* raw, char* dest, size_t destLen);

// Empty incoming never replaces. Otherwise the lexicographically smaller name wins.
const char* preferredOuiName(const char* current, const char* incoming);

// Local and group results never carry a name. A global prefix misses as Unknown
// when the table is present and as DataMissing when the table is empty.
OuiResult lookupOui(const OuiTable& table, const uint8_t mac[6]);

const char* macClassLabel(MacClass macClass);
const char* ouiStateLabel(OuiState state);

// Presentation clip is 31 characters. The inventory keeps the longer stored name.
void formatOuiLine(char* dest, size_t destLen, OuiState state, const char* manufacturer);
