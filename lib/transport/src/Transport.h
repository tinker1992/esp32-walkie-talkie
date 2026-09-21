#pragma once
#include <stdlib.h>
#include <stdint.h>

class OutputBuffer;

class Transport
{
public:
  // invoked from the radio receive callback with a whole received payload
  // (header already stripped). Used by the Opus path; the 8-bit PCM path
  // leaves this unset and keeps feeding the OutputBuffer directly.
  typedef void (*frame_rx_fn)(const uint8_t *data, int len, void *ctx);

protected:
  // audio buffer for samples we need to send
  uint8_t *m_buffer = NULL;
  int m_buffer_size = 0;
  int m_index = 0;
  int m_header_size;

  OutputBuffer *m_output_buffer = NULL;

  frame_rx_fn m_frame_rx = NULL;
  void *m_frame_rx_ctx = NULL;

  virtual void send() = 0;

public:
  Transport(OutputBuffer *output_buffer, size_t buffer_size);
  int set_header(const int header_size, const uint8_t *header);
  void add_sample(int16_t sample);
  // send one already-formed payload (e.g. a single Opus packet) as one frame
  int add_frame(const uint8_t *payload, size_t len);
  void set_frame_receiver(frame_rx_fn fn, void *ctx);
  void flush();
  virtual bool begin() = 0;
};
