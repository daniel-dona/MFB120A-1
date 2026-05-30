#pragma once

#include <cstdint>
#include <cstddef>

#include "esphome/core/datatypes.h"
#include "esphome/components/uart/uart.h"

namespace esphome::ricecooker {

class MCUCommunicator {
 public:
  MCUCommunicator() = default;
  explicit MCUCommunicator(uart::UARTDevice *uart);

  void set_uart_device(uart::UARTDevice *uart) { uart_device_ = uart; }

  /// Call regularly from the main loop. Handles init state machine and UART communication.
  void loop();

  bool is_initialized() const { return initialized_; }

  // Display control
  void set_time(uint8_t hours, uint8_t minutes);
  void set_power(bool power);
  void set_sleep(bool sleep);

  // LED control
  enum class LED_ID {
    LED1 = 1,
    LED2,
    LED3,
    LED4,
    LED5,
    LED6,
    LED7,
    LED8,
    LED9_ORANGE,
    LED9_BLUE,
  };

  enum class LED_STATE {
    OFF = 0,
    ON = 1,
  };

  void set_led_status(LED_ID led, LED_STATE state);

  // Temperature getters (from received MCU data)
  uint8_t get_top_temperature() const { return top_temperature_; }
  uint8_t get_bottom_temperature() const { return bottom_temperature_; }

 private:
  void send_data();
  void receive_data();
  void write_data();
  uint16_t crc16(const uint8_t *data, size_t len);
  static uint8_t int_7seg(uint8_t value, bool dot);

  // UART communication
  uart::UARTDevice *uart_device_{nullptr};

  // Init state machine
  bool initialized_{false};
  uint8_t init_step_{0};
  uint32_t init_last_{0};
  static constexpr uint32_t INIT_INTERVAL_MS = 50;

  // Communication buffers
  uint8_t send_buffer_[11]{};
  uint8_t recv_buffer_[10]{};

  // Communication timing
  uint32_t mcu_interval_{100};
  uint32_t mcu_last_{0};

  // Display state
  uint8_t hours_{0};
  uint8_t minutes_{0};
  bool power_{false};
  bool sleep_{false};
  bool middle_dots_{true};

  // Temperature from MCU
  uint8_t top_temperature_{0};
  uint8_t bottom_temperature_{0};

  // LED status
  bool led1_status_{false};
  bool led2_status_{false};
  bool led3_status_{false};
  bool led4_status_{false};
  bool led5_status_{false};
  bool led6_status_{false};
  bool led7_status_{false};
  bool led8_status_{false};
  bool led9_orange_status_{false};
  bool led9_blue_status_{false};
};

}  // namespace esphome::ricecooker