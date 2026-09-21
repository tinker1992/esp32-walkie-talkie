#include "OpusStream.h"

#ifdef USE_OPUS_CODEC

#include <Arduino.h>
#include <string.h>
#include <stdlib.h>

#include <opus.h>

// decoded PCM ring capacity (in samples) and encoded-packet ring capacity (in
// bytes). Tuned for a handful of buffered 20 ms frames of jitter protection.
static const int PCM_RING_SAMPLES = 4096;
static const int ENC_RING_BYTES = 8192;

OpusStream::OpusStream(int sample_rate, int frame_ms, int bitrate, int complexity)
    : m_sample_rate(sample_rate),
      m_frame_samples(sample_rate / 1000 * frame_ms),
      m_bitrate(bitrate),
      m_complexity(complexity)
{
  m_tx_fill = 0;
  m_rx_enc_head = m_rx_enc_tail = m_rx_enc_used = 0;
  m_rx_pcm_head = m_rx_pcm_tail = m_rx_pcm_used = 0;
  m_tx_frame = nullptr;
  m_tx_packet = nullptr;
  m_rx_enc_ring = nullptr;
  m_rx_pcm_ring = nullptr;
  m_decode_scratch = nullptr;
  m_enc = nullptr;
  m_dec = nullptr;
  m_tx_packet_max = MAX_PACKET_BYTES;
}

OpusStream::~OpusStream()
{
  if (m_enc)
    opus_encoder_destroy((OpusEncoder *)m_enc);
  if (m_dec)
    opus_decoder_destroy((OpusDecoder *)m_dec);
  free(m_tx_frame);
  free(m_tx_packet);
  free(m_rx_enc_ring);
  free(m_rx_pcm_ring);
  free(m_decode_scratch);
  if (m_lock)
    vSemaphoreDelete(m_lock);
}

bool OpusStream::begin()
{
  int err = OPUS_OK;
  m_enc = opus_encoder_create((opus_int32)m_sample_rate, 1, OPUS_APPLICATION_VOIP, &err);
  if (!m_enc || err != OPUS_OK)
  {
    Serial.printf("Opus: encoder create failed (%d)\n", err);
    return false;
  }
  m_dec = opus_decoder_create((opus_int32)m_sample_rate, 1, &err);
  if (!m_dec || err != OPUS_OK)
  {
    Serial.printf("Opus: decoder create failed (%d)\n", err);
    return false;
  }
  opus_encoder_ctl((OpusEncoder *)m_enc, OPUS_SET_BITRATE(m_bitrate));
  opus_encoder_ctl((OpusEncoder *)m_enc, OPUS_SET_COMPLEXITY(m_complexity));
  opus_encoder_ctl((OpusEncoder *)m_enc, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
  opus_encoder_ctl((OpusEncoder *)m_enc, OPUS_SET_VBR(1));

  m_tx_frame = (int16_t *)malloc(sizeof(int16_t) * m_frame_samples);
  m_tx_packet = (uint8_t *)malloc(m_tx_packet_max);
  m_decode_scratch = (int16_t *)malloc(sizeof(int16_t) * (m_sample_rate * 12 / 100 + 64));
  m_rx_enc_ring = (uint8_t *)malloc(ENC_RING_BYTES);
  m_rx_pcm_ring = (int16_t *)malloc(sizeof(int16_t) * PCM_RING_SAMPLES);
  if (!m_tx_frame || !m_tx_packet || !m_decode_scratch || !m_rx_enc_ring || !m_rx_pcm_ring)
  {
    Serial.println("Opus: allocation failed");
    return false;
  }
  m_rx_enc_size = ENC_RING_BYTES;
  m_rx_pcm_size = PCM_RING_SAMPLES;
  memset(m_tx_frame, 0, sizeof(int16_t) * m_frame_samples);

  m_lock = xSemaphoreCreateMutex();
  Serial.printf("Opus: ready @ %d Hz, frame %d samples, %d bps\n",
                m_sample_rate, m_frame_samples, m_bitrate);
  return true;
}

// ---------------- transmit ----------------

void OpusStream::write_pcm(const int16_t *samples, int count)
{
  int i = 0;
  while (i < count)
  {
    int room = m_frame_samples - m_tx_fill;
    int take = (count - i < room) ? (count - i) : room;
    memcpy(m_tx_frame + m_tx_fill, samples + i, take * sizeof(int16_t));
    m_tx_fill += take;
    i += take;
    if (m_tx_fill == m_frame_samples)
    {
      int enc_len = opus_encode((OpusEncoder *)m_enc, m_tx_frame, m_frame_samples,
                                m_tx_packet, m_tx_packet_max);
      m_tx_fill = 0;
      if (enc_len > 0 && m_sender)
        m_sender(m_tx_packet, enc_len, m_sender_ctx);
      else if (enc_len < 0)
        Serial.printf("Opus: encode error %d\n", enc_len);
    }
  }
}

void OpusStream::flush()
{
  if (m_tx_fill > 0)
  {
    memset(m_tx_frame + m_tx_fill, 0, (m_frame_samples - m_tx_fill) * sizeof(int16_t));
    int enc_len = opus_encode((OpusEncoder *)m_enc, m_tx_frame, m_frame_samples,
                              m_tx_packet, m_tx_packet_max);
    m_tx_fill = 0;
    if (enc_len > 0 && m_sender)
      m_sender(m_tx_packet, enc_len, m_sender_ctx);
  }
}

// ---------------- receive ----------------

void OpusStream::enqueue_encoded(const uint8_t *packet, int len)
{
  if (!m_rx_enc_ring || len < 1 || len > MAX_PACKET_BYTES)
    return;
  xSemaphoreTake(m_lock, portMAX_DELAY);
  int need = 1 + len;
  if (m_rx_enc_used + need <= m_rx_enc_size)
  {
    m_rx_enc_ring[m_rx_enc_tail] = (uint8_t)len;
    m_rx_enc_tail = (m_rx_enc_tail + 1) % m_rx_enc_size;
    for (int i = 0; i < len; i++)
    {
      m_rx_enc_ring[m_rx_enc_tail] = packet[i];
      m_rx_enc_tail = (m_rx_enc_tail + 1) % m_rx_enc_size;
    }
    m_rx_enc_used += need;
  }
  // else: overflow, silently drop this packet
  xSemaphoreGive(m_lock);
}

bool OpusStream::pull_encoded(uint8_t *dst, int max_len, int &out_len)
{
  xSemaphoreTake(m_lock, portMAX_DELAY);
  if (m_rx_enc_used == 0)
  {
    xSemaphoreGive(m_lock);
    return false;
  }
  int len = m_rx_enc_ring[m_rx_enc_head];
  m_rx_enc_head = (m_rx_enc_head + 1) % m_rx_enc_size;
  m_rx_enc_used -= 1;
  if (len > max_len)
    len = max_len; // defensive; packets are bounded on enqueue
  for (int i = 0; i < len; i++)
  {
    dst[i] = m_rx_enc_ring[m_rx_enc_head];
    m_rx_enc_head = (m_rx_enc_head + 1) % m_rx_enc_size;
  }
  m_rx_enc_used -= len;
  xSemaphoreGive(m_lock);
  out_len = len;
  return len > 0;
}

void OpusStream::push_pcm(const int16_t *samples, int count)
{
  xSemaphoreTake(m_lock, portMAX_DELAY);
  for (int i = 0; i < count; i++)
  {
    if (m_rx_pcm_used >= m_rx_pcm_size)
      break; // PCM ring overflow - drop the rest
    m_rx_pcm_ring[m_rx_pcm_tail] = samples[i];
    m_rx_pcm_tail = (m_rx_pcm_tail + 1) % m_rx_pcm_size;
    m_rx_pcm_used++;
  }
  xSemaphoreGive(m_lock);
}

int OpusStream::read_pcm(int16_t *samples, int count)
{
  if (!m_dec)
  {
    memset(samples, 0, count * sizeof(int16_t));
    return count;
  }

  // refill the decoded ring by pulling + decoding whole packets
  int scratch_cap = m_sample_rate * 12 / 100 + 64;
  for (;;)
  {
    int used;
    xSemaphoreTake(m_lock, portMAX_DELAY);
    used = m_rx_pcm_used;
    xSemaphoreGive(m_lock);
    if (used >= count)
      break;
    uint8_t pkt[MAX_PACKET_BYTES];
    int plen;
    if (!pull_encoded(pkt, MAX_PACKET_BYTES, plen))
      break; // nothing (more) to decode
    int n = opus_decode((OpusDecoder *)m_dec, pkt, plen, m_decode_scratch, scratch_cap, 0);
    if (n > 0)
      push_pcm(m_decode_scratch, n);
  }

  // drain exactly `count` samples, filling with silence on underrun
  xSemaphoreTake(m_lock, portMAX_DELAY);
  for (int i = 0; i < count; i++)
  {
    if (m_rx_pcm_used > 0)
    {
      samples[i] = m_rx_pcm_ring[m_rx_pcm_head];
      m_rx_pcm_head = (m_rx_pcm_head + 1) % m_rx_pcm_size;
      m_rx_pcm_used--;
    }
    else
    {
      samples[i] = 0;
    }
  }
  xSemaphoreGive(m_lock);
  return count;
}

#endif // USE_OPUS_CODEC
