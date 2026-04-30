//idf 4.4.4
#include <string.h>
#include <cmath>
#include <driver/gpio.h>
#include <driver/timer.h>
#include <driver/uart.h>
#include <esp_event.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <esp_netif.h>
#include "esp_wifi.h"

#include <ableton/Link.hpp>
#include "midi_parser.h"

#define LED GPIO_NUM_4  // Ensure this is the correct pin for the R32 D1
#define PRINT_LINK_STATE false

#define USB_UART UART_NUM_0   // USB UART
#define MIDI_UART UART_NUM_2  // Hardware MIDI UART
#define USB_TX_PIN GPIO_NUM_1  // TXD0
#define USB_RX_PIN GPIO_NUM_3  // RXD0
#define MIDI_TX_PIN GPIO_NUM_17
#define MIDI_RX_PIN GPIO_NUM_16
#define USB_MIDI true
#define LINK_TICK_PERIOD 100

// Different lengths for different beat positions (in ticks)
#define LENGTH_NORMAL          1    // Short beep for regular beats
#define LENGTH_16BEAT          20    // Longer beep for measure start
#define LENGTH_8BEAT           10    // Medium-long beep for half measure
#define LENGTH_4BEAT           5    // Medium beep for quarter measure

#define MIDI_TIMING_CLOCK 0xF8
#define MIDI_START 0xFA
#define MIDI_STOP 0xFC
#define MIDI_CONTINUE 0xFB

static const char *TAG = "LINK_APP";

// WiFi AP configuration
#define WIFI_SSID "Ableton-Link-ESP32"
#define WIFI_PASS "1234567890"
#define WIFI_CHANNEL 1
#define WIFI_MAX_CONNECTIONS 4

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
    wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
    ESP_LOGI(TAG, "Client " MACSTR " connected", MAC2STR(event->mac));
  } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
    wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
    ESP_LOGI(TAG, "Client " MACSTR " disconnected", MAC2STR(event->mac));
  }
}

static void initialize_wifi_ap(void) {
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));

  wifi_config_t wifi_config = {};
  memcpy(wifi_config.ap.ssid, WIFI_SSID, strlen(WIFI_SSID));
  memcpy(wifi_config.ap.password, WIFI_PASS, strlen(WIFI_PASS));
  wifi_config.ap.ssid_len = strlen(WIFI_SSID);
  wifi_config.ap.channel = WIFI_CHANNEL;
  wifi_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
  wifi_config.ap.max_connection = WIFI_MAX_CONNECTIONS;

  if (strlen(WIFI_PASS) == 0) {
    wifi_config.ap.authmode = WIFI_AUTH_OPEN;
  }

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  ESP_LOGI(TAG, "WiFi AP initialized with SSID: %s", WIFI_SSID);
}

static void send_midi_message(const uint8_t *data, size_t length) {
  // Send to hardware UART
  uart_write_bytes(MIDI_UART, (const char *)data, length);
  uart_wait_tx_done(MIDI_UART, 1);

  // Send to USB UART
  uart_write_bytes(USB_UART, (const char *)data, length);
  uart_wait_tx_done(USB_UART, 1);
}

void IRAM_ATTR timer_isr(void *userParam) {
  static BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  
  timer_group_clr_intr_status_in_isr(TIMER_GROUP_0, TIMER_0);
  timer_group_enable_alarm_in_isr(TIMER_GROUP_0, TIMER_0);

  vTaskNotifyGiveFromISR(userParam, &xHigherPriorityTaskWoken);
  if (xHigherPriorityTaskWoken) {
    portYIELD_FROM_ISR();
  }
}

void timerGroup0Init(int timerPeriodUS, void *userParam) {
  timer_config_t config = {
    .alarm_en = TIMER_ALARM_EN,
    .counter_en = TIMER_PAUSE,
    .intr_type = TIMER_INTR_LEVEL,
    .counter_dir = TIMER_COUNT_UP,
    .auto_reload = TIMER_AUTORELOAD_EN,
    .divider = 80  // A more stable value that still provides good precision
  };

  timer_init(TIMER_GROUP_0, TIMER_0, &config);
  timer_set_counter_value(TIMER_GROUP_0, TIMER_0, 0);
  timer_set_alarm_value(TIMER_GROUP_0, TIMER_0, timerPeriodUS);
  timer_enable_intr(TIMER_GROUP_0, TIMER_0);
  timer_isr_register(TIMER_GROUP_0, TIMER_0, &timer_isr, userParam, 
                    ESP_INTR_FLAG_IRAM,  // Keep ISR in IRAM for consistent timing
                    nullptr);

  timer_start(TIMER_GROUP_0, TIMER_0);
}

void initUartPort(uart_port_t port, int txPin, int rxPin) {
  uart_config_t uart_config = {
    .baud_rate = 31250,
    .data_bits = UART_DATA_8_BITS,
    .parity = UART_PARITY_DISABLE,
    .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    .rx_flow_ctrl_thresh = 0,
    .source_clk = UART_SCLK_APB
  };

  uart_param_config(port, &uart_config);
  uart_set_pin(port, txPin, rxPin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  uart_driver_install(port, 512, 0, 0, NULL, 0);
}

void tickTask(void *userParam) {
  ableton::Link link(117.0f);
  link.enable(true);
  link.enableStartStopSync(true);

  initUartPort(USB_UART, USB_TX_PIN, USB_RX_PIN);
  initUartPort(MIDI_UART, MIDI_TX_PIN, MIDI_RX_PIN);

  // Initialize MIDI parser for incoming clock/start/stop detection
  midi_parser_init();

  timerGroup0Init(LINK_TICK_PERIOD, xTaskGetCurrentTaskHandle());

  gpio_set_direction(LED, GPIO_MODE_OUTPUT);

  bool was_connected = false;
  int64_t start_wait_time = esp_timer_get_time();
  bool force_start = false;
  bool midi_transport_running = false;
  // Initialized to -1 so the very first tick always sends a clock.
  // Also reset explicitly on MIDI START so clocks begin without delay.
  static int lastTicks = -1;

  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    // Process incoming MIDI messages from MIDI_UART
    // Only play-state changes (START/STOP) touch Link here.
    // Tempo is applied separately below so MIDI is the sole BPM source.
    uint8_t midi_byte;
    int bytes_read = uart_read_bytes(MIDI_UART, &midi_byte, 1, 0);
    while (bytes_read > 0) {
      midi_message_type_t msg = midi_parser_process_byte(midi_byte);

      switch (msg) {
        case MIDI_MSG_START: {
          midi_transport_running = true;
          lastTicks = -1;  // Force clock output on the very next tick
          auto ls = link.captureAppSessionState();
          const auto t = link.clock().micros();
          ls.forceBeatAtTime(0.0, t, 4.0);  // 4-beat quantum = standard Ableton bar
          ls.setIsPlaying(true, t);
          link.commitAppSessionState(ls);
          break;
        }
        case MIDI_MSG_CONTINUE: {
          midi_transport_running = true;
          auto ls = link.captureAppSessionState();
          ls.setIsPlaying(true, link.clock().micros());
          link.commitAppSessionState(ls);
          break;
        }
        case MIDI_MSG_STOP: {
          midi_transport_running = false;
          auto ls = link.captureAppSessionState();
          ls.setIsPlaying(false, link.clock().micros());
          link.commitAppSessionState(ls);
          break;
        }
        default:
          break;
      }

      bytes_read = uart_read_bytes(MIDI_UART, &midi_byte, 1, 0);
    }

    // When MIDI clock is active, push the MIDI-derived tempo into Link,
    // but only when the value has actually changed (the EMA updates once per
    // quarter note, so this fires at most ~2x/sec at 120 BPM — not every tick).
    // When MIDI clock is absent, Link's own session tempo governs the outgoing clock.
    if (midi_parser_is_clock_active() && midi_parser_is_tempo_ready()) {
      static float last_pushed_tempo = 0.0f;
      float midi_tempo = midi_parser_get_tempo();
      if (fabsf(midi_tempo - last_pushed_tempo) > 0.01f) {
        auto ls = link.captureAppSessionState();
        ls.setTempo(midi_tempo, link.clock().micros());
        link.commitAppSessionState(ls);
        last_pushed_tempo = midi_tempo;
      }
    }

    // Check peer status
    bool is_connected = link.numPeers() > 0;
    if (!is_connected && !force_start && (esp_timer_get_time() - start_wait_time >= 3000000)) {  // 60 seconds in microseconds
      force_start = true;
    }
    
    if (is_connected != was_connected) {
      if (is_connected) {
        // Send MIDI reset sequence when connection is established
        const uint8_t stop_msg[] = {MIDI_STOP};
        send_midi_message(stop_msg, 1);
        vTaskDelay(pdMS_TO_TICKS(1));  // Small delay

        const uint8_t start_msg[] = {MIDI_START};
        send_midi_message(start_msg, 1);
      }
      was_connected = is_connected;
    }

    const auto state = link.captureAppSessionState();
    const auto quantum = 16.0;
    const auto time = link.clock().micros();
    
    // Calculate both regular and adjusted phases
    const auto phase = state.phaseAtTime(time, quantum);
    
    // Calculate ticks with consistent timing
    const int ticks = std::floor(state.beatAtTime(time, quantum) * 24);

    static int length = LENGTH_NORMAL;
    static int lastBeat = -1;
    
    // Use regular phase for LED timing
    const int currentBeat = static_cast<int>(std::floor(phase));
    const int beatInQuantum = currentBeat % static_cast<int>(quantum);
    const double beatFraction = phase - std::floor(phase);
    const int ticksInBeat = static_cast<int>(beatFraction * 150);
    
    bool crossedBeat = (currentBeat != lastBeat);
    
    if (is_connected || force_start || midi_transport_running) {
      if (crossedBeat) {
        // Update beat characteristics based on position in quantum
        if (beatInQuantum == 0) {  // First beat of quantum (measure start)
          length = LENGTH_16BEAT;
          // Re-sync USB UART slave at every quantum boundary
          const uint8_t usb_stop = MIDI_STOP;
          uart_write_bytes(USB_UART, (const char *)&usb_stop, 1);
          uart_wait_tx_done(USB_UART, 1);
          const uint8_t spp_zero[] = {0xF2, 0x00, 0x00};
          uart_write_bytes(USB_UART, (const char *)spp_zero, sizeof(spp_zero));
          uart_wait_tx_done(USB_UART, 1);
          const uint8_t usb_start = MIDI_START;
          uart_write_bytes(USB_UART, (const char *)&usb_start, 1);
          uart_wait_tx_done(USB_UART, 1);
        } else if (beatInQuantum == 8) {  // Middle of measure (8th beat)
          length = LENGTH_8BEAT;
        } else if (beatInQuantum == 4 || beatInQuantum == 12) {  // Quarter points
          length = LENGTH_4BEAT;
        } else {  // Regular beats
          length = LENGTH_NORMAL;
        }
        lastBeat = currentBeat;
      }
      
      // Determine if we should be playing based on position within beat
      bool shouldPlay = ticksInBeat < length;
      
      gpio_set_level(LED, shouldPlay);
      
      static bool was_playing = false;
      bool is_playing = state.isPlaying();
      
      // Send MIDI Stop/Start when play state changes
      if (was_playing != is_playing) {
        if (!is_playing) {
          // When stopping, send Stop
          const uint8_t stop_msg = MIDI_STOP;
          send_midi_message(&stop_msg, 1);
        } else {
          // When starting, send Stop then Start to ensure phase alignment
          const uint8_t stop_msg = MIDI_STOP;
          send_midi_message(&stop_msg, 1);

          const uint8_t start_msg = MIDI_START;
          send_midi_message(&start_msg, 1);
        }
        was_playing = is_playing;
      }

      // After forceBeatAtTime(0) the timeline resets: ticks jumps back to 0
      // while lastTicks still holds the pre-reset value. Snap it down so the
      // very first tick fires immediately instead of waiting 4 beats to catch up.
      if (ticks < lastTicks) {
        lastTicks = ticks - 1;
      }

      // Send exactly one 0xF8 timing clock per 24ppqn tick transition
      if (ticks > lastTicks) {
        const uint8_t timing_msg = MIDI_TIMING_CLOCK;
        send_midi_message(&timing_msg, 1);

        // Send SPP to USB UART every 16th note (6 MIDI clocks per 16th note at 24ppqn)
        if (ticks % 6 == 0) {
          uint16_t spp_pos = (uint16_t)((ticks / 6) % 32768);
          uint8_t spp_msg[] = {0xF2, (uint8_t)(spp_pos & 0x7F), (uint8_t)((spp_pos >> 7) & 0x7F)};
          uart_write_bytes(USB_UART, (const char *)spp_msg, sizeof(spp_msg));
          uart_wait_tx_done(USB_UART, 1);
        }
      }
    } else {
      gpio_set_level(LED, 0);
    }
    lastTicks = ticks;
  }
}

extern "C" void app_main() {
  ESP_ERROR_CHECK(nvs_flash_init());
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  // Create WiFi AP interface
  esp_netif_create_default_wifi_ap();

  // Initialize WiFi driver
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  // Setup WiFi as Access Point
  initialize_wifi_ap();

  xTaskCreate(tickTask, "ticks", 16384, nullptr, 10, nullptr);
}
