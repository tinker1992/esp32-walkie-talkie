#include "RotaryEncoder.h"

#ifdef USE_OLED_DISPLAY

#include <Arduino.h>

void RotaryEncoder::begin(int pin_a, int pin_b)
{
  m_pin_a = pin_a;
  m_pin_b = pin_b;
  pinMode(m_pin_a, INPUT_PULLUP);
  pinMode(m_pin_b, INPUT_PULLUP);
  m_state = digitalRead(m_pin_a);
}

// Count one detent per falling edge of phase A, direction from phase B level.
// Tolerates the coarse (~8 ms) polling cadence available from the UI tick.
int RotaryEncoder::read()
{
  int a = digitalRead(m_pin_a);
  int detent = 0;
  if (a != m_state)
  {
    if (a == LOW) // falling edge -> one click
      detent = (digitalRead(m_pin_b) == HIGH) ? 1 : -1;
    m_state = a;
  }
  return detent;
}

#endif // USE_OLED_DISPLAY
