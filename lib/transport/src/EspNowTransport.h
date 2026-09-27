#pragma once

#include "Transport.h"

class OutputBuffer;

class EspNowTransport: public Transport {
private:
  uint8_t m_wifi_channel;
  bool m_encrypt = false;
  uint8_t m_lmk[16] = {0};
protected:
  void send();
public:
  EspNowTransport(OutputBuffer *output_buffer, uint8_t wifi_channel);
  // AES-128 link-layer key, injected by the application (the library must not
  // include the project's config.h - it collides with another lib's header)
  void set_lmk(const uint8_t *lmk);
  virtual bool begin() override;
  friend void receiveCallback(const uint8_t *macAddr, const uint8_t *data, int dataLen);
};
