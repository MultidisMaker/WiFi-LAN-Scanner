#pragma once

#include "BoardConfig.h"

class ScannerController;

#if WLS_TEST_MODE
ScannerController& deviceScanner();
void deviceUiLoop();
#endif
