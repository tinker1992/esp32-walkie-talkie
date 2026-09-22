#include <freertos/FreeRTOS.h>
#include <driver/i2s.h>
#include <driver/gpio.h>

// WiFi credentials
#define WIFI_SSID << YOUR_SSID >>
#define WIFI_PSWD << YOUR_PASSWORD >>

// sample rate for the system
#define SAMPLE_RATE 16000

// are you using an I2S microphone - comment this if you want to use an analog mic and ADC input
// #define USE_I2S_MIC_INPUT

// I2S Microphone Settings

// Which channel is the I2S microphone on? I2S_CHANNEL_FMT_ONLY_LEFT or I2S_CHANNEL_FMT_ONLY_RIGHT
// Generally they will default to LEFT - but you may need to attach the L/R pin to GND
#define I2S_MIC_CHANNEL I2S_CHANNEL_FMT_ONLY_LEFT
// #define I2S_MIC_CHANNEL I2S_CHANNEL_FMT_ONLY_RIGHT

// Analog Microphone Settings - ADC1_CHANNEL_7 is GPIO35 (classic ESP32 only)
#define ADC_MIC_CHANNEL ADC1_CHANNEL_7

// speaker settings
#define USE_I2S_SPEAKER_OUTPUT

// Which I2S peripherals to use. The ESP32-S3 has two independent I2S ports, so
// we keep the microphone and the speaker on separate peripherals and no longer
// have to tear down / reinstall one shared port every time we switch between
// transmit and receive. The classic ESP32 keeps the original I2S_NUM_0 for both.
#ifdef CONFIG_IDF_TARGET_ESP32S3
#define MIC_I2S_PORT I2S_NUM_0
#define SPEAKER_I2S_PORT I2S_NUM_1
#else
#define MIC_I2S_PORT I2S_NUM_0
#define SPEAKER_I2S_PORT I2S_NUM_0
#endif

// Pin mapping. GPIO numbers differ between the classic ESP32 and the ESP32-S3,
// so the whole set is selected with a target guard.
#ifdef CONFIG_IDF_TARGET_ESP32S3
// --- ESP32-S3: INMP441 on I2S0, MAX98357 on I2S1 ---
// Avoid GPIO26-32 (flash) and, on octal-PSRAM modules, GPIO33-37.
#define I2S_MIC_SERIAL_CLOCK GPIO_NUM_15      // INMP441 SCK
#define I2S_MIC_LEFT_RIGHT_CLOCK GPIO_NUM_16  // INMP441 WS
#define I2S_MIC_SERIAL_DATA GPIO_NUM_17       // INMP441 SD
#define I2S_SPEAKER_SERIAL_CLOCK GPIO_NUM_4   // MAX98357 BCLK
#define I2S_SPEAKER_LEFT_RIGHT_CLOCK GPIO_NUM_5   // MAX98357 LRC
#define I2S_SPEAKER_SERIAL_DATA GPIO_NUM_6    // MAX98357 DIN
// MAX98357 has no shutdown line - keep this -1
#define I2S_SPEAKER_SD_PIN -1
// transmit (push-to-talk) button
#define GPIO_TRANSMIT_BUTTON 1
#else
// --- Classic ESP32: original pinout ---
#define I2S_MIC_SERIAL_CLOCK GPIO_NUM_18
#define I2S_MIC_LEFT_RIGHT_CLOCK GPIO_NUM_19
#define I2S_MIC_SERIAL_DATA GPIO_NUM_21
#define I2S_SPEAKER_SERIAL_CLOCK GPIO_NUM_18
#define I2S_SPEAKER_LEFT_RIGHT_CLOCK GPIO_NUM_19
#define I2S_SPEAKER_SERIAL_DATA GPIO_NUM_5
// Shutdown line if you have this wired up or -1 if you don't
#define I2S_SPEAKER_SD_PIN GPIO_NUM_22
// transmit button
#define GPIO_TRANSMIT_BUTTON 23
#endif

// Which LED pin do you want to use? TinyPico LED or the builtin LED of a generic ESP32 board?
// Comment out this line to use the builtin LED of a generic ESP32 board
// #define USE_LED_GENERIC

// Which transport do you want to use? ESP_NOW or UDP?
// comment out this line to use UDP
// #define USE_ESP_NOW

// On which wifi channel (1-11) should ESP-Now transmit? The default ESP-Now channel on ESP32 is channel 1
#define ESP_NOW_WIFI_CHANNEL 1

// ESP-NOW link-layer encryption (AES-128) via a pre-shared Local Master Key.
// Every walkie-talkie in the same group MUST use the identical 16-byte LMK or
// they will not be able to read each other. CHANGE this default key for your
// own build; to go back to plaintext, comment out USE_ESP_NOW_LMK.
#define USE_ESP_NOW_LMK
#define ESP_NOW_LMK                    \
  0xE3, 0x0A, 0x7C, 0x51, 0x9B, 0x24,  \
  0x6D, 0xF8, 0x11, 0xA0, 0x3E, 0xC5,  \
  0x77, 0x02, 0x9D, 0x4B

// Opus codec settings (only used when USE_OPUS_CODEC is defined)
// 20 ms frames at 16 kHz -> 320 samples/frame. ~24 kbps mono VBR is a good
// voice quality/bandwidth trade-off and keeps each packet well under the
// 250-byte ESP-NOW limit.
#define OPUS_FRAME_MS 20
#define OPUS_BITRATE 24000
#define OPUS_COMPLEXITY 5

// In case all transport packets need a header (to avoid interference with other applications or walkie talkie sets), 
// specify TRANSPORT_HEADER_SIZE (the length in bytes of the header) in the next line, and define the transport header in config.cpp
#define TRANSPORT_HEADER_SIZE 0
extern uint8_t transport_header[TRANSPORT_HEADER_SIZE];


// i2s config for using the internal ADC
extern i2s_config_t i2s_adc_config;
// i2s config for reading from of I2S
extern i2s_config_t i2s_mic_Config;
// i2s microphone pins
extern i2s_pin_config_t i2s_mic_pins;
// i2s speaker pins
extern i2s_pin_config_t i2s_speaker_pins;
