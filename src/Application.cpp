#include <Arduino.h>
#include <driver/i2s.h>
#include <WiFi.h>

#include "Application.h"
#include "I2SMEMSSampler.h"
#include "ADCSampler.h"
#include "I2SOutput.h"
#include "DACOutput.h"
#include "UdpTransport.h"
#include "EspNowTransport.h"
#include "OutputBuffer.h"
#include "config.h"

#ifdef USE_OPUS_CODEC
#include "Transport.h"
#include "OpusStream.h"
#endif

#ifdef USE_OLED_DISPLAY
#include "WalkieDisplay.h"
#endif

#ifdef ARDUINO_TINYPICO
#include "TinyPICOIndicatorLed.h"
#else
#include "GenericDevBoardIndicatorLed.h"
#endif

static void application_task(void *param)
{
  // delegate onto the application
  Application *application = reinterpret_cast<Application *>(param);
  application->loop();
}

#ifdef USE_OPUS_CODEC
// Opus packet -> transport frame (one Opus packet per ESP-NOW/UDP datagram)
static void opus_tx_sender(const uint8_t *packet, int len, void *ctx)
{
  reinterpret_cast<Transport *>(ctx)->add_frame(packet, len);
}
// received frame -> Opus decode ring (called from the radio callback)
static void opus_rx_frame(const uint8_t *data, int len, void *ctx)
{
  reinterpret_cast<OpusStream *>(ctx)->enqueue_encoded(data, len);
}
#endif

Application::Application()
{
#ifdef USE_OPUS_CODEC
  m_output_buffer = NULL; // unused on the Opus path
  m_opus = new OpusStream(SAMPLE_RATE, OPUS_FRAME_MS, OPUS_BITRATE, OPUS_COMPLEXITY);
#else
  m_output_buffer = new OutputBuffer(300 * 16);
#endif
#ifdef USE_I2S_MIC_INPUT
  m_input = new I2SMEMSSampler(MIC_I2S_PORT, i2s_mic_pins, i2s_mic_Config,128);
#else
  m_input = new ADCSampler(ADC_UNIT_1, ADC1_CHANNEL_7, i2s_adc_config);
#endif

#ifdef USE_I2S_SPEAKER_OUTPUT
  m_output = new I2SOutput(SPEAKER_I2S_PORT, i2s_speaker_pins);
#else
  m_output = new DACOutput(SPEAKER_I2S_PORT);
#endif

#ifdef USE_ESP_NOW
  EspNowTransport *espnow_transport = new EspNowTransport(m_output_buffer, ESP_NOW_WIFI_CHANNEL);
#ifdef USE_ESP_NOW_LMK
  {
    // 16-byte pre-shared key from config.h; src/config.h resolves correctly here
    static const uint8_t lmk[16] = {ESP_NOW_LMK};
    espnow_transport->set_lmk(lmk);
  }
#endif
  m_transport = espnow_transport;
#else
  m_transport = new UdpTransport(m_output_buffer);
#endif

  m_transport->set_header(TRANSPORT_HEADER_SIZE,transport_header);

#ifdef USE_OPUS_CODEC
  // route decoded Opus frames straight through the transport
  m_transport->set_frame_receiver(opus_rx_frame, m_opus);
  m_opus->set_sender(opus_tx_sender, m_transport);
#endif

#ifdef USE_OLED_DISPLAY
  m_display = new WalkieDisplay();
  m_ui_level = 0;
#endif

#ifdef ARDUINO_TINYPICO
  m_indicator_led = new TinyPICOIndicatorLed();
#else
  m_indicator_led = new GenericDevBoardIndicatorLed();
#endif

  if (I2S_SPEAKER_SD_PIN != -1)
  {
    pinMode(I2S_SPEAKER_SD_PIN, OUTPUT);
  }
}

void Application::begin()
{
  // show a flashing indicator that we are trying to connect
  m_indicator_led->set_default_color(0);
  m_indicator_led->set_is_flashing(true, 0xff0000);
  m_indicator_led->begin();

  Serial.print("My IDF Version is: ");
  Serial.println(esp_get_idf_version());

  // bring up WiFi
  WiFi.mode(WIFI_STA);
#ifndef USE_ESP_NOW
  WiFi.begin(WIFI_SSID, WIFI_PSWD);
  if (WiFi.waitForConnectResult() != WL_CONNECTED)
  {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }
  // this has a dramatic effect on packet RTT
  WiFi.setSleep(WIFI_PS_NONE);
  Serial.print("My IP Address is: ");
  Serial.println(WiFi.localIP());
#else
  // but don't connect if we're using ESP NOW
  WiFi.disconnect();
#endif
  Serial.print("My MAC Address is: ");
  Serial.println(WiFi.macAddress());
  // do any setup of the transport
  m_transport->begin();
  // connected so show a solid green light
  m_indicator_led->set_default_color(0x00ff00);
  m_indicator_led->set_is_flashing(false, 0x00ff00);
  // setup the transmit button
  pinMode(GPIO_TRANSMIT_BUTTON, INPUT_PULLDOWN);
  // start off with i2S output running
  m_output->start(SAMPLE_RATE);
#ifdef USE_OPUS_CODEC
  if (!m_opus->begin())
  {
    Serial.println("Opus codec failed to initialise");
  }
#else
  // flush all samples received during startup
  m_output_buffer->flush();
#endif

#ifdef USE_OLED_DISPLAY
  analogSetPinAttenuation(BATT_ADC_PIN, ADC_11db);
  m_display->begin(OLED_I2C_SDA, OLED_I2C_SCL, OLED_I2C_ADDR, OLED_COL_OFFSET);
#endif

  // start the main task for the application (bigger stack for Opus decode)
  TaskHandle_t task_handle;
#ifdef USE_OPUS_CODEC
  const uint32_t task_stack = 24576;
#else
  const uint32_t task_stack = 8192;
#endif
  xTaskCreate(application_task, "application_task", task_stack, this, 1, &task_handle);
}

// application task - coordinates everything
void Application::loop()
{
  int16_t *samples = reinterpret_cast<int16_t *>(malloc(sizeof(int16_t) * 128));
  // continue forever
  while (true)
  {
    // do we need to start transmitting?
    if (digitalRead(GPIO_TRANSMIT_BUTTON))
    {
      Serial.println("Started transmitting");
      m_indicator_led->set_is_flashing(true, 0xff0000);
      // stop the output as we're switching into transmit mode
      m_output->stop();
      // start the input to get samples from the microphone
      m_input->start();
      // transmit for at least 1 second or while the button is pushed
      unsigned long start_time = millis();
      while (millis() - start_time < 1000 || digitalRead(GPIO_TRANSMIT_BUTTON))
      {
        // read samples from the microphone
        int samples_read = m_input->read(samples, 128);
#ifdef USE_OPUS_CODEC
        // accumulate into Opus frames and transmit each encoded packet
        m_opus->write_pcm(samples, samples_read);
#else
        // and send them over the transport
        for (int i = 0; i < samples_read; i++)
        {
          m_transport->add_sample(samples[i]);
        }
#endif
#ifdef USE_OLED_DISPLAY
        ui_service(true, start_time, samples, samples_read);
#endif
      }
#ifdef USE_OPUS_CODEC
      // pad + send the trailing partial frame
      m_opus->flush();
#else
      // send all packets still in the transport buffer
      m_transport->flush();
#endif
      // finished transmitting stop the input and start the output
      Serial.println("Finished transmitting");
      m_indicator_led->set_is_flashing(false, 0xff0000);
      m_input->stop();
      m_output->start(SAMPLE_RATE);
    }
    // while the transmit button is not pushed and 1 second has not elapsed
    Serial.println("Started Receiving");
    if (I2S_SPEAKER_SD_PIN != -1)
    {
      digitalWrite(I2S_SPEAKER_SD_PIN, HIGH);
    }
    unsigned long start_time = millis();
    while (millis() - start_time < 1000 || !digitalRead(GPIO_TRANSMIT_BUTTON))
    {
#ifdef USE_OPUS_CODEC
      // decode queued Opus packets (as needed) into the speaker buffer
      m_opus->read_pcm(samples, 128);
#else
      // read from the output buffer (which should be getting filled by the transport)
      m_output_buffer->remove_samples(samples, 128);
#endif
      // and send the samples to the speaker
      m_output->write(samples, 128);
#ifdef USE_OLED_DISPLAY
      ui_service(false, 0, samples, 128);
#endif
    }
    if (I2S_SPEAKER_SD_PIN != -1)
    {
      digitalWrite(I2S_SPEAKER_SD_PIN, LOW);
    }
    Serial.println("Finished Receiving");
  }
}

#ifdef USE_OLED_DISPLAY
static int peak_level(const int16_t *s, int n)
{
  long peak = 0;
  for (int i = 0; i < n; i++)
  {
    long v = s[i] < 0 ? -s[i] : s[i];
    if (v > peak)
      peak = v;
  }
  int lvl = (int)(peak * 8 / 12000); // ~12000 counts ≈ full-scale voice
  if (lvl < 0) lvl = 0;
  if (lvl > 8) lvl = 8;
  return lvl;
}

void Application::ui_service(bool transmitting, uint32_t tx_start_ms, const int16_t *samples, int count)
{
  if (!m_display)
    return;

  int lvl = peak_level(samples, count);
  m_ui_level = (m_ui_level + lvl) / 2; // light smoothing

  static uint32_t last_render = 0;
  uint32_t now = millis();
  if (now - last_render < 100)
    return;
  last_render = now;

  DisplayModel m;
  m.transmitting = transmitting;
  m.rx_active = !transmitting && m_ui_level > 0;
  m.level = m_ui_level;
  m.tx_seconds = transmitting ? (now - tx_start_ms) / 1000 : 0;
  m.callsign = WALKIE_CALLSIGN;
  m.channel = ESP_NOW_WIFI_CHANNEL;
  m.encrypted = false;
#ifdef USE_ESP_NOW_LMK
  m.encrypted = true;
#endif
#ifdef USE_OPUS_CODEC
  m.codec = OPUS_LABEL;
#else
  m.codec = "PCM 8bit";
#endif

  int mv = (int)(analogReadMilliVolts(BATT_ADC_PIN) * BATT_DIVIDER);
  int pct = (mv - BATT_MV_EMPTY) * 100 / (BATT_MV_FULL - BATT_MV_EMPTY);
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  m.battery_pct = pct;

  m_display->render(m);
}
#endif
