#pragma once

// Display mapping provenance, copied from the installed vendor example and not invented:
// GFX Library for Arduino 1.4.6
// C:\Users\LelandJohnson\Documents\Arduino\libraries\GFX_Library_for_Arduino\examples\PDQgraphicstest\Arduino_GFX_dev_device.h
// Branch LILYGO_T_DISPLAY_S3_PRO:
//   GFX_BL 48
//   Arduino_ESP32SPI(DC 9, CS 39, SCK 18, MOSI 17, MISO 8)
//   Arduino_ST7796(RST 47, rotation 0, IPS true, 222 x 480, col offset 49, row offset 0, col offset 2 49, row offset 2 0)
// The non-Pro T-Display-S3 variant (170x320, LCD_BL 38) is not used.
//
// Touch bus provenance: the same TF-LAPTOP-00 T-Display-S3-Pro diagnostic at
// C:\Users\LelandJohnson\Documents\MakerNexus\GarageController\Firmware\src\pro_profile.h
// records SDA 5 / SCL 6, SensorLib 0.1.6 CST226SE (address 0x5A, chip id 0xA8),
// and reset/irq left at -1 because no installed Pro source names those GPIOs.
// This increment does not assign a touch reset or interrupt pin.

static constexpr int kPanelWidth = 222;
static constexpr int kPanelHeight = 480;
static constexpr int kPanelColOffset = 49;
static constexpr int kPanelRowOffset = 0;

static constexpr int kTftDc = 9;
static constexpr int kTftCs = 39;
static constexpr int kTftSck = 18;
static constexpr int kTftMosi = 17;
static constexpr int kTftMiso = 8;
static constexpr int kTftRst = 47;
static constexpr int kTftBl = 48;

static constexpr int kTouchSda = 5;
static constexpr int kTouchScl = 6;
static constexpr int kTouchRst = -1;
static constexpr int kTouchIrq = -1;

// Future host discovery, not executed in this increment, may examine at most this
// many usable addresses even when the joined subnet is larger.
static constexpr uint32_t kFutureScanHostCap = 256;

static constexpr unsigned long kScannerTransitionMs = 200;
static constexpr unsigned long kWifiConnectTimeoutMs = 20000;
static constexpr int kMaxScanResults = 12;
static constexpr int kMaxSsidLen = 32;
static constexpr int kMaxPassLen = 63;
