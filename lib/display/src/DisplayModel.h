#pragma once

#ifdef USE_OLED_DISPLAY

#include <stdint.h>

// Everything the display needs, assembled by the application each refresh.
// Only fields the firmware can actually obtain are populated; RSSI / peer count
// are intentionally omitted (they need an ESP-NOW recv-info upgrade).
struct DisplayModel
{
  bool transmitting;      // PTT currently held
  bool rx_active;         // decoded audio present right now (drives level meter)
  int level;              // 0..8 audio level bar (mic in TX, output in RX)
  uint32_t tx_seconds;    // seconds since transmission started
  const char *callsign;   // group / callsign text
  int channel;            // ESP-NOW channel
  int battery_pct;        // 0..100, or -1 if not wired
  bool encrypted;         // ESP-NOW LMK active
  const char *codec;      // "OPUS 16k/20ms" or "PCM 8bit"
};

#endif // USE_OLED_DISPLAY
