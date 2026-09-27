#pragma once

#ifdef USE_OLED_DISPLAY

#include "DisplayModel.h"
#include <stdint.h>

// Dependency-free SSD1306 (128x64, I2C, page addressing) driver for the walkie
// UI. Experiment phase: a single dashboard page - no page navigation.
class WalkieDisplay
{
public:
  WalkieDisplay();
  ~WalkieDisplay();

  // I2C pins + 7-bit address are supplied by the application so this module
  // stays independent of the project's config.h (which would otherwise collide
  // with an unrelated "config.h" shipped by another library).
  bool begin(uint8_t sda, uint8_t scl, uint8_t addr);
  void showBoot();

  void render(const DisplayModel &model);

private:
  void draw_dashboard(const DisplayModel &m);

  // framebuffer primitives (coords: x 0..127, y 0..63)
  void clear();
  void pixel(int x, int y, bool on);
  void hline(int x, int y, int w);
  void box(int x, int y, int w, int h, bool fill);
  void text(int x, int y, const char *s); // y = top of the 8px cell
  int  textw(const char *s);
  void rtext(int y, const char *s);       // right-aligned
  void vu(int x, int y, int level);
  void battery(int x, int y, int pct);
  void flush();
  void cmd(uint8_t c);

  uint8_t *m_fb;         // 1024-byte display buffer
  uint8_t m_addr;        // SSD1306 I2C address
  bool m_ready;
  uint32_t m_boot_until_ms;
};

#endif // USE_OLED_DISPLAY
