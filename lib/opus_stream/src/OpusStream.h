#pragma once

// This whole module is only compiled when the Opus codec path is selected.
// The classic ESP32 build (8-bit PCM transport) never includes this header, so
// it also never pulls in the opus library.
#ifdef USE_OPUS_CODEC

#include <stdint.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// OpusStream wraps a fixed-point Opus encoder + decoder and adapts the
// project's pull-based sample flow to Opus' frame-based API.
//
//  * TX side: PCM int16 samples are accumulated until a full Opus frame is
//    available, encoded, and handed to a sender callback (one Opus packet per
//    transport packet).
//  * RX side: complete encoded packets received from the transport are pushed
//    into an "encoded frame" ring by the radio callback (which only copies
//    bytes - it never decodes). The application loop pulls a packet out,
//    decodes it into a 16-bit PCM ring, and drains that ring to the speaker.
//
// Doing the (CPU heavy, stack hungry) decode on the application task instead of
// the WiFi callback task keeps the radio callback short and safe.
class OpusStream
{
public:
  // callback invoked with a single encoded Opus packet to transmit
  typedef void (*sender_fn)(const uint8_t *packet, int len, void *ctx);

  OpusStream(int sample_rate, int frame_ms, int bitrate, int complexity);
  ~OpusStream();

  bool begin();

  int frame_samples() const { return m_frame_samples; }
  int sample_rate() const { return m_sample_rate; }

  void set_sender(sender_fn fn, void *ctx) { m_sender = fn; m_sender_ctx = ctx; }

  // ---- transmit path ----
  void write_pcm(const int16_t *samples, int count);
  void flush(); // pad + send the trailing partial frame

  // ---- receive path ----
  // called from the radio callback: only enqueues raw packet bytes
  void enqueue_encoded(const uint8_t *packet, int len);
  // called from the application loop: decodes as needed and fills `samples`
  int read_pcm(int16_t *samples, int count);

private:
  bool pull_encoded(uint8_t *dst, int max_len, int &out_len); // copy+consume one packet
  void push_pcm(const int16_t *samples, int count);

  int m_sample_rate;
  int m_frame_samples;   // e.g. 320 for 16 kHz / 20 ms
  int m_bitrate;
  int m_complexity;

  void *m_enc;           // OpusEncoder*
  void *m_dec;           // OpusDecoder*

  // TX frame accumulator
  int16_t *m_tx_frame;
  int m_tx_fill;
  uint8_t *m_tx_packet;
  int m_tx_packet_max;

  // TX sequence numbering (lets the RX detect lost packets and run PLC)
  uint8_t m_tx_seq;

  // DC blocker state (one-pole high-pass on the mic input)
  int32_t m_dc_x_prev;
  int32_t m_dc_y_prev;

  // RX sync state for gap detection
  uint8_t m_rx_expected;
  bool m_rx_synced;

  sender_fn m_sender = nullptr;
  void *m_sender_ctx = nullptr;

  // RX encoded-packet ring (each entry: [uint8 len][bytes])
  uint8_t *m_rx_enc_ring;
  int m_rx_enc_size;
  int m_rx_enc_head; // read position (application task)
  int m_rx_enc_tail; // write position (radio callback)
  int m_rx_enc_used;

  // RX decoded PCM ring (int16)
  int16_t *m_rx_pcm_ring;
  int m_rx_pcm_size;   // capacity in samples
  int m_rx_pcm_head;
  int m_rx_pcm_tail;
  int m_rx_pcm_used;   // samples available

  int16_t *m_decode_scratch; // scratch for opus_decode output

  SemaphoreHandle_t m_lock;

  static const int MAX_PACKET_BYTES = 240; // <= ESP-NOW 250 - header margin
};

#endif // USE_OPUS_CODEC
