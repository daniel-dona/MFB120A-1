#pragma once

#include <cstdint>
#include <cstddef>

#include "esphome/core/datatypes.h"
#include "esphome/components/uart/uart.h"

namespace esphome::ricecooker {

/// MCU UART protocol handler (0xAA/0x55 framing).
///
/// TX (11 bytes, ESP32→MCU):
///   [0] 0x55 header
///   [1] 0x07 length
///   [2] control byte (relay, indicators)
///   [3-6] 7-segment display digits
///   [7-8] LED status banks
///   [9-10] CRC-16/XMODEM
///
/// RX (10 bytes, MCU→ESP32):
///   [0] 0xAA header
///   [1] length
///   [2] command/buttons (0x81=TIMER, 0x82=CANCEL, 0x84=SELECT, 0x88=START)
///   [3] top/lid temperature °C (direct, no conversion)
///   [4] bottom/plate temperature °C (direct, no conversion)
///   [5] unknown (possibly voltage ADC or status)
///   [6-7] unknown
///   [8-9] CRC-16/XMODEM
class MCUCommunicator {
 public:
  MCUCommunicator() = default;
  explicit MCUCommunicator(uart::UARTDevice *uart);

  void set_uart_device(uart::UARTDevice *uart) { uart_device_ = uart; }

  /// Call regularly from main loop. Handles init state machine and UART I/O.
  void loop();

  /// Returns true once the init sequence has completed.
  bool is_initialized() const { return initialized_; }

  // --- Display control ---
  void set_time(uint8_t hours, uint8_t minutes);
  void set_power(bool power);
  void set_sleep(bool sleep);

  // --- LED control ---
  enum class LED_ID {
    LED1 = 1, LED2, LED3, LED4, LED5,
    LED6, LED7, LED8, LED9_ORANGE, LED9_BLUE,
  };
  enum class LED_STATE { OFF = 0, ON = 1 };

  void set_led_status(LED_ID led, LED_STATE state);

  // --- Sensor getters ---
  uint8_t get_top_temperature() const { return top_temperature_; }
  uint8_t get_bottom_temperature() const { return bottom_temperature_; }
  uint8_t get_voltage() const { return voltage_; }

  // --- Button press callback type ---
  /// Callback receives the button command byte (0x81=TIMER, 0x82=CANCEL, 0x84=SELECT, 0x88=START).
  using ButtonCallback = void(*)(uint8_t command);
  void set_button_callback(ButtonCallback cb) { button_callback_ = cb; }

 private:
  void send_data();
  void receive_data();
  void write_data();
  uint16_t crc16(const uint8_t *data, size_t len) const;
  static uint8_t int_7seg(uint8_t value, bool dot);

  // UART
  uart::UARTDevice *uart_device_{nullptr};

  // Init state machine (non-blocking, replaces vTaskDelay)
  bool initialized_{false};
  uint8_t init_step_{0};
  uint32_t init_last_{0};
  static constexpr uint32_t INIT_INTERVAL_MS = 50;

  // Communication buffers
  uint8_t send_buffer_[11]{};
  uint8_t recv_buffer_[16]{};  // Extra space for robustness

  // Communication timing
  uint32_t mcu_interval_{100};
  uint32_t mcu_last_{0};

  // Display state
  uint8_t hours_{0};
  uint8_t minutes_{0};
  bool power_{false};
  bool sleep_{false};

  // Sensor data from MCU
  uint8_t top_temperature_{0};
  uint8_t bottom_temperature_{0};
  uint8_t voltage_{200};  // Default: ~230V AC (ADC value ~200)

  // Button callback
  ButtonCallback button_callback_{nullptr};

  // LED status
  bool led1_{false}, led2_{false}, led3_{false}, led4_{false}, led5_{false};
  bool led6_{false}, led7_{false}, led8_{false}, led9_orange_{false}, led9_blue_{false};
};

}  // namespace esphome::ricecooker