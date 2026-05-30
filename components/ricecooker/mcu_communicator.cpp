#include "mcu_communicator.h"

#include "esphome/core/log.h"

namespace esphome::ricecooker {

static const char *const TAG = "mcu_communicator";
static const uint8_t RECV_HEADER = 0xaa;
static const uint8_t SEND_HEADER = 0x55;

// Initialization command sequence (sent during init state machine)
static const uint8_t INIT_COMMANDS[][11] = {
    {0x55, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc7, 0x18},   // Black
    {0x55, 0x07, 0x10, 0x00, 0x00, 0x00, 0x00, 0x1f, 0x1f, 0x00, 0xf0},   // Beep
    {0x55, 0x07, 0x10, 0xff, 0xff, 0xff, 0xff, 0x1f, 0x1f, 0x8a, 0x20},   // Beep
    {0x55, 0x07, 0x10, 0x00, 0x00, 0x00, 0x00, 0x1f, 0x1f, 0x00, 0xf0},   // Beep
    {0x55, 0x07, 0x10, 0xff, 0xff, 0xff, 0xff, 0x1f, 0x1f, 0x8a, 0x20},   // Beep
    {0x55, 0x07, 0x00, 0xff, 0xff, 0xff, 0xff, 0x1f, 0x1f, 0xbd, 0x5b},   // All
    {0x55, 0x07, 0x00, 0xff, 0xff, 0xff, 0xff, 0x02, 0x18, 0xb8, 0x93},   // ?
    {0x55, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc7, 0x18},   // Black
};
static constexpr uint8_t NUM_INIT_COMMANDS = sizeof(INIT_COMMANDS) / sizeof(INIT_COMMANDS[0]);

MCUCommunicator::MCUCommunicator(uart::UARTDevice *uart) : uart_device_(uart) {}

void MCUCommunicator::loop() {
  // Non-blocking initialization state machine
  if (!initialized_) {
    if (millis() - init_last_ >= INIT_INTERVAL_MS) {
      init_last_ = millis();
      if (init_step_ < NUM_INIT_COMMANDS) {
        uart_device_->write_array(INIT_COMMANDS[init_step_], 11);
        init_step_++;
      } else {
        initialized_ = true;
        ESP_LOGD(TAG, "MCU initialization complete");
      }
    }
    return;  // Don't do anything else until init is complete
  }

  // Normal operation: periodic send/receive
  if (millis() > mcu_last_ + mcu_interval_) {
    mcu_last_ = millis();
    send_data();
    receive_data();
  }
}

void MCUCommunicator::send_data() {
  write_data();
  uart_device_->write_array(send_buffer_, 11);
}

void MCUCommunicator::receive_data() {
  uint8_t ch;
  uint8_t n = 0;

  while (uart_device_->available()) {
    ch = uart_device_->read();

    if (ch == RECV_HEADER || n == 10) {
      n = 0;
    }

    if (n < 10) {
      recv_buffer_[n] = ch;
      n++;
    } else {
      ESP_LOGD(TAG, "MCU recv buffer full, skipping data");
      continue;
    }
  }

  if (n < 10) {
    ESP_LOGVV(TAG, "Incomplete MCU data packet received (%d bytes)", n);
    return;
  }

  // CRC check (skip header byte)
  uint16_t crc = crc16(recv_buffer_ + sizeof(uint8_t), 7);

  if (recv_buffer_[0] != RECV_HEADER || recv_buffer_[8] != ((crc >> 8) & 0xFF) ||
      recv_buffer_[9] != (crc & 0xFF)) {
    ESP_LOGD(TAG, "Got an invalid package from MCU");
    return;
  }

  // Update temperature values from received data
  top_temperature_ = recv_buffer_[3];
  bottom_temperature_ = recv_buffer_[4];

  switch (recv_buffer_[2]) {
    case 129:
      ESP_LOGD(TAG, "TIMER");
      break;
    case 130:
      ESP_LOGD(TAG, "CANCEL");
      break;
    case 132:
      ESP_LOGD(TAG, "SELECT");
      break;
    case 136:
      ESP_LOGD(TAG, "START");
      break;
  }
}

uint16_t MCUCommunicator::crc16(const uint8_t *data, size_t len) {
  uint16_t crc = 0x0000;

  while (len--) {
    crc ^= (*data++) << 8;
    for (int i = 0; i < 8; i++) {
      if (crc & 0x8000) {
        crc = (crc << 1) ^ 0x1021;
      } else {
        crc <<= 1;
      }
    }
  }
  return crc;
}

uint8_t MCUCommunicator::int_7seg(uint8_t value, bool dot) {
  static const uint8_t SEGMENT_TABLE[] = {
      0b00111111,  // 0
      0b00000110,  // 1
      0b01011011,  // 2
      0b01001111,  // 3
      0b01100110,  // 4
      0b01101101,  // 5
      0b01111101,  // 6
      0b00000111,  // 7
      0b01111111,  // 8
      0b01101111,  // 9
  };

  uint8_t byte = SEGMENT_TABLE[value % 10];
  if (dot) {
    byte |= 0b10000000;
  }
  return byte;
}

void MCUCommunicator::write_data() {
  send_buffer_[0] = SEND_HEADER;  // Header
  send_buffer_[1] = 0x07;         // Command length

  send_buffer_[2] = 0b00000001;  // General ON
  if (power_) {
    send_buffer_[2] |= 0b00000100;  // Relay
  }
  if (sleep_) {
    send_buffer_[2] |= 0b00100000;  // Sleep
  }

  send_buffer_[3] = int_7seg(hours_ / 10, false);
  send_buffer_[4] = int_7seg(hours_ % 10, true);   // Middle dot
  send_buffer_[5] = int_7seg(minutes_ / 10, true);  // Middle dot
  send_buffer_[6] = int_7seg(minutes_ % 10, false);

  // LED1-LED5 status
  send_buffer_[7] = 0b00000000;
  if (led1_status_) send_buffer_[7] |= 0b00000001;
  if (led2_status_) send_buffer_[7] |= 0b00000010;
  if (led3_status_) send_buffer_[7] |= 0b00000100;
  if (led4_status_) send_buffer_[7] |= 0b00001000;
  if (led5_status_) send_buffer_[7] |= 0b00010000;

  // LED6-LED9 status
  send_buffer_[8] = 0b00000000;
  if (led6_status_) send_buffer_[8] |= 0b00000001;
  if (led7_status_) send_buffer_[8] |= 0b00000010;
  if (led8_status_) send_buffer_[8] |= 0b00000100;
  if (led9_orange_status_) send_buffer_[8] |= 0b00001000;
  if (led9_blue_status_) send_buffer_[8] |= 0b00010000;

  uint16_t crc = crc16(send_buffer_ + sizeof(uint8_t), 8);
  send_buffer_[9] = (crc >> 8) & 0xFF;
  send_buffer_[10] = crc & 0xFF;
}

void MCUCommunicator::set_time(uint8_t hours, uint8_t minutes) {
  hours_ = hours;
  minutes_ = minutes;
}

void MCUCommunicator::set_power(bool power) { power_ = power; }

void MCUCommunicator::set_sleep(bool sleep) { sleep_ = sleep; }

void MCUCommunicator::set_led_status(LED_ID led, LED_STATE state) {
  bool state_bool = (state == LED_STATE::ON);

  switch (led) {
    case LED_ID::LED1:
      led1_status_ = state_bool;
      break;
    case LED_ID::LED2:
      led2_status_ = state_bool;
      break;
    case LED_ID::LED3:
      led3_status_ = state_bool;
      break;
    case LED_ID::LED4:
      led4_status_ = state_bool;
      break;
    case LED_ID::LED5:
      led5_status_ = state_bool;
      break;
    case LED_ID::LED6:
      led6_status_ = state_bool;
      break;
    case LED_ID::LED7:
      led7_status_ = state_bool;
      break;
    case LED_ID::LED8:
      led8_status_ = state_bool;
      break;
    case LED_ID::LED9_ORANGE:
      led9_orange_status_ = state_bool;
      break;
    case LED_ID::LED9_BLUE:
      led9_blue_status_ = state_bool;
      break;
  }
}

}  // namespace esphome::ricecooker