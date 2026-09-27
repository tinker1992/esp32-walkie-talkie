#pragma once

#include <stdint.h>

class Output;
class I2SSampler;
class Transport;
class OutputBuffer;
class IndicatorLed;
#ifdef USE_OPUS_CODEC
class OpusStream;
#endif
#ifdef USE_OLED_DISPLAY
class WalkieDisplay;
#endif

class Application
{
private:
  Output *m_output;
  I2SSampler *m_input;
  Transport *m_transport;
  IndicatorLed *m_indicator_led;
  OutputBuffer *m_output_buffer;
#ifdef USE_OPUS_CODEC
  OpusStream *m_opus;
#endif
#ifdef USE_OLED_DISPLAY
  WalkieDisplay *m_display;
  int m_ui_level;
  void ui_service(bool transmitting, uint32_t tx_start_ms, const int16_t *samples, int count);
#endif

public:
  Application();
  void begin();
  void loop();
};
