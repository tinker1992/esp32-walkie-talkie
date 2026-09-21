#pragma once

class Output;
class I2SSampler;
class Transport;
class OutputBuffer;
class IndicatorLed;
#ifdef USE_OPUS_CODEC
class OpusStream;
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

public:
  Application();
  void begin();
  void loop();
};
