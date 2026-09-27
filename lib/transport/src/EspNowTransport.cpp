#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <string.h>
#include "OutputBuffer.h"
#include "EspNowTransport.h"

const int MAX_ESP_NOW_PACKET_SIZE = 250;
const uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static EspNowTransport *instance = NULL;

void receiveCallback(const uint8_t *macAddr, const uint8_t *data, int dataLen)
{
  // annoyingly we can't pass an param into this so we need to do a bit of hack to access the EspNowTransport instance
  int header_size = instance->m_header_size;
  
  // first m_header_size bytes of m_buffer are the expected header
  if ((dataLen > header_size) && (dataLen<=MAX_ESP_NOW_PACKET_SIZE) && (memcmp(data,instance->m_buffer,header_size) == 0))
  {
    // bring-up aid: throttled reception counter (1 line per 25 valid packets)
    static uint32_t rx_pkt_count = 0;
    if ((++rx_pkt_count % 25) == 1)
      Serial.printf("[RX] valid packets=%u len=%d\n", (unsigned)rx_pkt_count, dataLen);
    if (instance->m_frame_rx)
    {
      // Opus path: hand the whole payload (minus header) to the frame receiver
      instance->m_frame_rx(data + header_size, dataLen - header_size, instance->m_frame_rx_ctx);
    }
    else
    {
      instance->m_output_buffer->add_samples(data + header_size, dataLen - header_size);
    }
  }
}

bool EspNowTransport::begin()
{
  // Set Wifi channel
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(m_wifi_channel, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
  
  esp_err_t result = esp_now_init();
  if (result == ESP_OK)
  {
    Serial.println("ESPNow Init Success");
    esp_now_register_recv_cb(receiveCallback);
  }
  else
  {
    Serial.printf("ESPNow Init failed: %s\n", esp_err_to_name(result));
    return false;
  }
  // this will broadcast a message to everyone in range
  esp_now_peer_info_t peerInfo = {};
  memcpy(&peerInfo.peer_addr, broadcastAddress, 6);
  if (m_encrypt)
  {
    // ESP-NOW forbids encryption on multicast/broadcast peers
    // (esp_now_add_peer returns ESP_ERR_ESPNOW_ARG and the peer is never
    // registered, breaking all sends). LMK only works for unicast peers, so
    // broadcast stays plaintext; real confidentiality needs app-layer crypto
    // or a MAC-discovery + unicast-pairing step.
    Serial.println("ESP-NOW: LMK ignored - encryption unsupported for broadcast (plaintext)");
  }
  if (!esp_now_is_peer_exist(broadcastAddress))
  {
    result = esp_now_add_peer(&peerInfo);
    if (result != ESP_OK)
    {
      Serial.printf("Failed to add broadcast peer: %s\n", esp_err_to_name(result));
      return false;
    }
  }
  return true;
}

EspNowTransport::EspNowTransport(OutputBuffer *output_buffer, uint8_t wifi_channel) : Transport(output_buffer, MAX_ESP_NOW_PACKET_SIZE)
{
  instance = this;
  m_wifi_channel = wifi_channel;
}

void EspNowTransport::set_lmk(const uint8_t *lmk)
{
  memcpy(m_lmk, lmk, 16);
  m_encrypt = true;
}

void EspNowTransport::send()
{

  esp_err_t result = esp_now_send(broadcastAddress, m_buffer, m_index + m_header_size);
  if (result != ESP_OK)
  {
    Serial.printf("Failed to send: %s\n", esp_err_to_name(result));
  }
}
