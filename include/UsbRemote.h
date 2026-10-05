#pragma once

#include "ScannerController.h"

class ScannerUi;

// Production USB transport for Remote Protocol v1. The test image forwards
// "@R1 " lines here and keeps the HIL command parser separate.

void usbRemoteBind(ScannerUi* ui, ScannerController* scanner);
void usbRemotePoll();
bool usbRemoteStreaming();
void usbRemoteSubmitLine(const char* line);
void usbRemotePullOne();
void usbRemoteOversize();
