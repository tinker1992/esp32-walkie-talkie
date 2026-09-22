#pragma once

#ifdef USE_OLED_DISPLAY

// Minimal polling rotary encoder (KY-040 / EC11 quadrature). Call read() often
// (e.g. from the UI service tick); it returns the net detents since the last
// call (+1 clockwise, -1 counter-clockwise, 0 none). No interrupts needed.
class RotaryEncoder
{
public:
  void begin(int pin_a, int pin_b);
  int read();

private:
  int m_pin_a;
  int m_pin_b;
  int m_state; // last (A<<1|B)
};

#endif // USE_OLED_DISPLAY
