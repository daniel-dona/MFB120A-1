#include "mcu_communicator.h"
#include "esphome/core/log.h"

namespace esphome::ricecooker {

static const char *const TAG = "mcu_comm";

// Protocol headers
static const uint8_t RECV_HEADER = 0xAA;
static const uint8_t SEND_HEADER = 0x55;
static const uint8_t RECV_LEN = 10;

// Button command bytes from MCU
static const uint8_t BTN_TIMER  = 0x81;
static const uint8_t BTN_CANCEL = 0x82;
static const uint8_t BTN_SELECT = 0x84;
static const uint8_t BTN_START  = 0x88;

// Initialization command sequence (non-blocking, 50ms between each)
static const uint8_t INIT_COMMANDS[][11] = {
    {0x55, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc7, 0x18},  // Clear display
    {0x55, 0x07, 0x10, 0x00, 0x00, 0x00, 0x00, 0x1f, 0x1f, 0x00, 0xf0},  // Beep on
    {0x55, 0x07, 0x10, 0xff, 0xff, 0xff, 0xff, 0x1f, 0x1f, 0x8a, 0x20},  // All segments
    {0x55, 0x07, 0x10, 0x00, 0x00, 0x00, 0x00, 0x1f, 0x1f, 0x00, 0xf0},  // Beep off
    {0x55, 0x07, 0x10, 0xff, 0xff, 0xff, 0xff, 0x1f, 0x1f, 0x8a, 0x20},  // All segments
    {0x55, 0x07, 0x00, 0xff, 0xff, 0xff, 0xff, 0x1f, 0x1f, 0xbd, 0x5b},  // Test
    {0x55, 0x07, 0x00, 0xff, 0xff, 0xff, 0xff, 0x02, 0x18, 0xb8, 0x93},  // Status
    {0x55, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc7, 0x18},  // Clear display
};
static constexpr uint8_t NUM_INIT_COMMANDS = sizeof(INIT_COMMANDS) / sizeof(INIT_COMMANDS[0]);

MCUCommunicator::MCUCommunicator(uart::UARTDevice *uart) : uart_device_(uart) {}

void MCUCommunicator::loop() {
  // --- Non-blocking initialization state machine ---
  if (!initialized_) {
    if (millis() - init_last_ >= INIT_INTERVAL_MS) {
      init_last_ = millis();
      if (init_step_ < NUM_INIT_COMMANDS) {
        uart_device_->write_array(INIT_COMMANDS[init_step_], 11);
        init_step_++;
      } else {
        initialized_ = true;
        ESP_LOGI(TAG, "MCU initialization complete");
      }
    }
    // Read and discard any bytes during init (MCU might be sending status)
    while (uart_device_->available()) {
      uart_device_->read();
    }
    return;
  }

  // --- Normal operation: periodic send/receive ---
  if (millis() - mcu_last_ >= mcu_interval_) {
    mcu_last_ = millis();
    send_data();
  }
  // Always process received data
  receive_data();
}

void MCUCommunicator::send_data() {
  write_data();
  uart_device_->write_array(send_buffer_, 11);
  ESP_LOGVV(TAG, "TX: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
            send_buffer_[0], send_buffer_[1], send_buffer_[2], send_buffer_[3],
            send_buffer_[4], send_buffer_[5], send_buffer_[6], send_buffer_[7],
            send_buffer_[8], send_buffer_[9], send_buffer_[10]);
}

void MCUCommunicator::receive_data() {
  static uint8_t buf_idx = 0;
  uint8_t ch;

  while (uart_device_->available()) {
    ch = uart_device_->read();

    // Sync: 0xAA marks the start of a new packet
    if (ch == RECV_HEADER) {
      buf_idx = 0;
    }

    // Store byte (with overflow protection)
    if (buf_idx < sizeof(recv_buffer_)) {
      recv_buffer_[buf_idx++] = ch;
    } else {
      // Buffer overflow — resync
      buf_idx = 0;
      continue;
    }

    // Full packet received?
    if (buf_idx == RECV_LEN) {
      // Validate CRC over bytes [1..7] (skip header at [0])
      uint16_t crc = crc16(recv_buffer_ + 1, 7);

      if (recv_buffer_[0] != RECV_HEADER ||
          recv_buffer_[8] != ((crc >> 8) & 0xFF) ||
          recv_buffer_[9] != (crc & 0xFF)) {
        ESP_LOGD(TAG, "Invalid RX packet (CRC mismatch)");
        buf_idx = 0;
        continue;
      }

      // Valid packet — extract data
      top_temperature_ = recv_buffer_[3];
      bottom_temperature_ = recv_buffer_[4];
      // Byte [5] may contain voltage or status data from MCU
      // We store it for diagnostics but don't rely on it yet
      voltage_ = recv_buffer_[5];

      uint8_t cmd = recv_buffer_[2];
      if (cmd != 0) {
        ESP_LOGD(TAG, "RX: cmd=0x%02X top=%d°C bot=%d°C v=%d",
                 cmd, top_temperature_, bottom_temperature_, voltage_);
        // Notify button presses
        if (button_callback_ != nullptr) {
          button_callback_(cmd);
        }
      }

      ESP_LOGVV(TAG, "RX: top=%d bot=%d v=%d cmd=0x%02X",
                top_temperature_, bottom_temperature_, voltage_, cmd);

      buf_idx = 0;  // Ready for next packet
    }
  }
}

void MCUCommunicator::write_data() {
  send_buffer_[0] = SEND_HEADER;  // 0x55
  send_buffer_[1] = 0x07;         // Length

  // Control byte [2]
  send_buffer_[2] = 0b00000001;   // General ON flag
  if (power_) {
    send_buffer_[2] |= 0b00000100;  // Bottom heater relay
  }
  if (sleep_) {
    send_buffer_[2] |= 0b00100000;  // Sleep mode
  }

  // 7-segment display [3-6]
  send_buffer_[3] = int_7seg(hours_ / 10, false);
  send_buffer_[4] = int_7seg(hours_ % 10, true);    // Dot
  send_buffer_[5] = int_7seg(minutes_ / 10, true);   // Dot
  send_buffer_[6] = int_7seg(minutes_ % 10, false);

  // LED bank 1 [7]: LED1-LED5
  send_buffer_[7] = 0b00000000;
  if (led1_) send_buffer_[7] |= 0b00000001;
  if (led2_) send_buffer_[7] |= 0b00000010;
  if (led3_) send_buffer_[7] |= 0b00000100;
  if (led4_) send_buffer_[7] |= 0b00001000;
  if (led5_) send_buffer_[7] |= 0b00010000;

  // LED bank 2 [8]: LED6-LED9
  send_buffer_[8] = 0b00000000;
  if (led6_) send_buffer_[8] |= 0b00000001;
  if (led7_) send_buffer_[8] |= 0b00000010;
  if (led8_) send_buffer_[8] |= 0b00000100;
  if (led9_orange_) send_buffer_[8] |= 0b00001000;
  if (led9_blue_) send_buffer_[8] |= 0b00010000;

  // CRC-16/XMODEM over bytes [1..8] (skip header at [0])
  uint16_t crc = crc16(send_buffer_ + 1, 8);
  send_buffer_[9] = (crc >> 8) & 0xFF;
  send_buffer_[10] = crc & 0xFF;
}

uint16_t MCUCommunicator::crc16(const uint8_t *data, size_t len) const {
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

void MCUCommunicator::set_time(uint8_t hours, uint8_t minutes) {
  hours_ = hours;
  minutes_ = minutes;
}

void MCUCommunicator::set_power(bool power) { power_ = power; }
void MCUCommunicator::set_sleep(bool sleep) { sleep_ = sleep; }

void MCUCommunicator::set_led_status(LED_ID led, LED_STATE state) {
  bool on = (state == LED_STATE::ON);
  switch (led) {
    case LED_ID::LED1:         led1_ = on; break;
    case LED_ID::LED2:         led2_ = on; break;
    case LED_ID::LED3:         led3_ = on; break;
    case LED_ID::LED4:         led4_ = on; break;
    case LED_ID::LED5:         led5_ = on; break;
    case LED_ID::LED6:         led6_ = on; break;
    case LED_ID::LED7:         led7_ = on; break;
    case LED_ID::LED8:         led8_ = on; break;
    case LED_ID::LED9_ORANGE:  led9_orange_ = on; break;
    case LED_ID::LED9_BLUE:    led9_blue_ = on; break;
  }
}

}  // namespace esphome::ricecooker