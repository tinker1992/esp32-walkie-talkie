#include "WalkieDisplay.h"

#ifdef USE_OLED_DISPLAY

#include <Arduino.h>
#include <Wire.h>
#include <string.h>
#include <stdio.h>
#include "ssd1306_font.h"

static const int DISP_W = 128;
static const int DISP_H = 64;

static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

WalkieDisplay::WalkieDisplay()
    : m_fb(nullptr), m_addr(0x3C), m_ready(false), m_boot_until_ms(0) {}

WalkieDisplay::~WalkieDisplay()
{
  free(m_fb);
}

void WalkieDisplay::cmd(uint8_t c)
{
  Wire.beginTransmission(m_addr);
  Wire.write((uint8_t)0x00); // Co=0, D/C=0 -> command
  Wire.write(c);
  Wire.endTransmission();
}

bool WalkieDisplay::begin(uint8_t sda, uint8_t scl, uint8_t addr)
{
  m_addr = addr;
  Wire.begin(sda, scl);
  Wire.setClock(400000);

  m_fb = (uint8_t *)malloc(DISP_W * DISP_H / 8); // 1024 bytes
  if (!m_fb)
    return false;
  memset(m_fb, 0, DISP_W * DISP_H / 8);

  // SSD1306 init (page addressing)
  cmd(0xAE);            // display off
  cmd(0xD5); cmd(0x80); // clock div
  cmd(0xA8); cmd(0x3F); // multiplex ratio = 64
  cmd(0xD3); cmd(0x00); // display offset 0
  cmd(0x40);            // start line 0
  cmd(0x8D); cmd(0x14); // charge pump on
  cmd(0x20); cmd(0x02); // page addressing mode
  cmd(0xA1);            // segment remap
  cmd(0xC8);            // COM scan direction
  cmd(0xDA); cmd(0x12); // COM pins
  cmd(0x81); cmd(0xCF); // contrast
  cmd(0xD9); cmd(0xF1); // pre-charge period
  cmd(0xDB); cmd(0x40); // Vcomh deselect
  cmd(0xA4);            // resume RAM
  cmd(0xA6);            // non-inverted
  cmd(0xAF);            // display on

  m_ready = true;
  showBoot();
  return true;
}

void WalkieDisplay::flush()
{
  if (!m_ready)
    return;
  for (int page = 0; page < 8; page++)
  {
    cmd(0xB0 | page);
    cmd(0x00); // lower column start
    cmd(0x10); // upper column start
    Wire.beginTransmission(m_addr);
    Wire.write((uint8_t)0x40); // Co=0, D/C=1 -> data
    for (int x = 0; x < DISP_W; x++)
      Wire.write(m_fb[page * DISP_W + x]);
    Wire.endTransmission();
  }
}

// ---- primitives ----

void WalkieDisplay::clear() { memset(m_fb, 0, DISP_W * DISP_H / 8); }

void WalkieDisplay::pixel(int x, int y, bool on)
{
  if (x < 0 || x >= DISP_W || y < 0 || y >= DISP_H)
    return;
  int idx = (y / 8) * DISP_W + x;
  uint8_t bit = (uint8_t)(1 << (y % 8));
  if (on) m_fb[idx] |= bit; else m_fb[idx] &= ~bit;
}

void WalkieDisplay::hline(int x, int y, int w)
{
  for (int i = 0; i < w; i++)
    pixel(x + i, y, true);
}

void WalkieDisplay::box(int x, int y, int w, int h, bool fill)
{
  for (int j = 0; j < h; j++)
    for (int i = 0; i < w; i++)
    {
      bool edge = (i == 0 || j == 0 || i == w - 1 || j == h - 1);
      if (fill || edge)
        pixel(x + i, y + j, true);
    }
}

void WalkieDisplay::text(int x, int y, const char *s)
{
  while (*s)
  {
    int c = (uint8_t)*s;
    if (c < 0x20 || c > 0x7E) c = '?';
    const uint8_t *g = FONT5X7[c - 0x20];
    for (int col = 0; col < 5; col++)
    {
      uint8_t bits = g[col];
      for (int row = 0; row < 7; row++)
        if (bits & (1 << row))
          pixel(x + col, y + row, true);
    }
    x += 6;
    s++;
  }
}

int WalkieDisplay::textw(const char *s) { return (int)strlen(s) * 6 - 1; }

void WalkieDisplay::rtext(int y, const char *s) { text(DISP_W - 1 - textw(s), y, s); }

void WalkieDisplay::vu(int x, int base_y, int level)
{
  const int n = 10, bw = 4, gap = 2, maxh = 16;
  int lit = clampi(level, 0, 8) * n / 8;
  for (int i = 0; i < n; i++)
  {
    int h = 3 + (i * (maxh - 3)) / (n - 1);
    int bx = x + i * (bw + gap);
    box(bx, base_y - h, bw, h, i < lit);
  }
}

void WalkieDisplay::battery(int x, int y, int pct)
{
  box(x, y, 24, 10, false);
  box(x + 24, y + 3, 2, 4, true);
  if (pct > 0)
    box(x + 1, y + 1, (22 * clampi(pct, 0, 100)) / 100, 8, true);
}

// ---- boot + single dashboard page ----

void WalkieDisplay::showBoot()
{
  if (!m_ready)
    return;
  clear();
  text(0, 2, "S3 WALKIE");
  hline(0, 12, 84);
  text(0, 20, "Opus + ESP-NOW");
  text(0, 30, "+ AES128 LMK");
  text(0, 40, "+ OLED SSD1306");
  text(0, 52, "starting...");
  flush();
  m_boot_until_ms = millis() + 3000;
}

void WalkieDisplay::render(const DisplayModel &m)
{
  if (!m_ready)
    return;
  if (millis() < m_boot_until_ms)
  {
    showBoot();
    return;
  }
  clear();
  draw_dashboard(m);
  flush();
}

void WalkieDisplay::draw_dashboard(const DisplayModel &m)
{
  // header: mode dot + RX/TX (with talk timer), callsign/channel right
  box(0, 1, 6, 6, true);
  char head[28];
  if (m.transmitting)
  {
    text(9, 0, "TX");
    int mm = (int)(m.tx_seconds / 60), ss = (int)(m.tx_seconds % 60);
    snprintf(head, sizeof(head), "%s %02d:%02d", m.callsign, mm, ss);
  }
  else
  {
    text(9, 0, "RX");
    snprintf(head, sizeof(head), "%s CH%d", m.callsign, m.channel);
  }
  rtext(0, head);
  hline(0, 11, 128);

  // level meter (mic input while transmitting, output level otherwise)
  text(0, 28, "LVL");
  vu(34, 36, m.level);

  // battery + encryption
  text(0, 44, "bat");
  battery(24, 42, m.battery_pct);
  if (m.battery_pct >= 0)
  {
    char b[8];
    snprintf(b, sizeof(b), "%d%%", m.battery_pct);
    text(54, 44, b);
  }
  rtext(44, m.encrypted ? "AES ON" : "AES off");

  // codec line
  text(0, 56, m.codec);
}

#endif // USE_OLED_DISPLAY
