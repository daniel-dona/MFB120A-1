#include "mcu_communicator.h"
#include "esphome/core/log.h"

namespace esphome {
namespace ricecooker {

static const char *const TAG = "mcu_communicator";
static const uint8_t RECV_HEADER = 0xaa;
static const uint8_t SEND_HEADER = 0x55;

MCUCommunicator::MCUCommunicator(uart::UARTDevice *parent) : Component() {
    this->uart_device_ = parent;
}

void MCUCommunicator::setup() {
    uint8_t reset[][11] = {
            {0x55,0x07,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xc7,0x18}, //Black
            {0x55,0x07,0x10,0x00,0x00,0x00,0x00,0x1f,0x1f,0x00,0xf0}, //Beep
            {0x55,0x07,0x10,0xff,0xff,0xff,0xff,0x1f,0x1f,0x8a,0x20}, //Beep
            {0x55,0x07,0x00,0xff,0xff,0xff,0xff,0x1f,0x1f,0xbd,0x5b}, //All
            {0x55,0x07,0x00,0xff,0xff,0xff,0xff,0x02,0x18,0xb8,0x93}  //?
        };

        //register_service(&RiceCooker::send_command, "send_command", {"command"});

        this->uart_device_->write_array(reset[0], 11);
        vTaskDelay(50);
        this->uart_device_->write_array(reset[1], 11);
        vTaskDelay(50);
        this->uart_device_->write_array(reset[2], 11);
        vTaskDelay(50);
        this->uart_device_->write_array(reset[1], 11);
        vTaskDelay(50);
        this->uart_device_->write_array(reset[2], 11);
        vTaskDelay(50);
        this->uart_device_->write_array(reset[3], 11);
        vTaskDelay(50);
        this->uart_device_->write_array(reset[0], 11);
        vTaskDelay(50);
}

void MCUCommunicator::loop() {
    if (millis() > mcu_last + mcu_interval) {
        mcu_last = millis();
        send_data();
        receive_data();
    }
}

void MCUCommunicator::send_data() {
    write_data();
    if (this->uart_device_ != nullptr) {
        this->uart_device_->write_array(send_buffer, 11);
    }
}

void MCUCommunicator::receive_data() {
    uint8_t ch;
    uint8_t n = 0;

    if (this->uart_device_ == nullptr) {
        return;
    }

    while (this->uart_device_->available()) {
        ch = this->uart_device_->read();

        if (ch == RECV_HEADER || n == 10) {
            n = 0;
        }

        // Extra safety: prevent buffer overflow
        if (n < 10) {
            recv_buffer[n] = ch;
            n++;
        } else {
            ESP_LOGD(TAG, "MCU recv buffer full, skipping data");
            continue;
        }
    }

    // Extra safety: ensure we have enough data for CRC check
    if (n < 10) {
        ESP_LOGD(TAG, "Incomplete MCU data packet received");
        return;
    }

    // For CRC header is skipped
    uint16_t crc = crc16(recv_buffer + sizeof(uint8_t), 7); 

    if (recv_buffer[0] != RECV_HEADER || recv_buffer[8] != ((crc >> 8) & 0xFF) || recv_buffer[9] != (crc & 0xFF)) {
        ESP_LOGD(TAG, "Got an invalid package from MCU");
        return;
    }

    // Update temperature values from received data
    top_temperature = recv_buffer[3];
    bottom_temperature = recv_buffer[4];
    
    switch(recv_buffer[2]){
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
    uint16_t crc = 0x0000;  // Initialize CRC

    while (len--) {
        crc ^= (*data++) << 8; // XOR data byte with CRC, and shift left to treat as most significant byte
        for (int i = 0; i < 8; i++) {
            if (crc & 0x8000) { // Check MOST significant bit
                crc = (crc << 1) ^ 0x1021; // Left shift and XOR polynomial
            } else {
                crc <<= 1; // Left shift
            }
        }
    }
    return crc;
}

uint8_t MCUCommunicator::int_7seg(uint8_t value, bool dot) {
    uint8_t table[] = {
        0b00111111, // 0
        0b00000110, // 1
        0b01011011, // 2
        0b01001111, // 3
        0b01100110, // 4
        0b01101101, // 5
        0b01111101, // 6
        0b00000111, // 7
        0b01111111, // 8
        0b01101111, // 9
    };

    uint8_t byte = table[value % 10];

    if (dot) byte |= 0b10000000;

    return byte;
}

void MCUCommunicator::write_data() {
    send_buffer[0] = SEND_HEADER; // Header
    send_buffer[1] = 0x07; // Command length

    send_buffer[2] = 0b00000000;

    send_buffer[2] |= 0b00000001; // Some kind of general ON?
    //buffer[2] |= 0b00010000; // Beep
    //buffer[2] |= 0b00000100; // RL

    if (power) {
        send_buffer[2] |= 0b00000100;
    }

    if (sleep) {
        send_buffer[2] |= 0b00100000; // Sleep
    }

    send_buffer[3] = int_7seg(hours / 10, false);
    send_buffer[4] = int_7seg(hours % 10, true);
    send_buffer[5] = int_7seg(minutes / 10, true);
    send_buffer[6] = int_7seg(minutes % 10, false);

    send_buffer[7] = 0b00000000;

    // Set LED1-LED5 status in send_buffer[7]
    if (led1_status) {
        send_buffer[7] |= 0b00000001; // LED1: bit 0
    }
    if (led2_status) {
        send_buffer[7] |= 0b00000010; // LED2: bit 1
    }
    if (led3_status) {
        send_buffer[7] |= 0b00000100; // LED3: bit 2
    }
    if (led4_status) {
        send_buffer[7] |= 0b00001000; // LED4: bit 3
    }
    if (led5_status) {
        send_buffer[7] |= 0b00010000; // LED5: bit 4
    }

    send_buffer[8] = 0b00000000;

    // Set LED6-LED9 status in send_buffer[8]
    if (led6_status) {
        send_buffer[8] |= 0b00000001; // LED6: bit 0
    }
    if (led7_status) {
        send_buffer[8] |= 0b00000010; // LED7: bit 1
    }
    if (led8_status) {
        send_buffer[8] |= 0b00000100; // LED8: bit 2
    }
    if (led9_orange_status) {
        send_buffer[8] |= 0b00001000; // LED9_ORANGE: bit 3
    }
    if (led9_blue_status) {
        send_buffer[8] |= 0b00010000; // LED9_BLUE: bit 4
    }


    uint16_t crc = crc16(send_buffer + sizeof(uint8_t), 8);
    send_buffer[9] = (crc >> 8) & 0xFF;
    send_buffer[10] = crc & 0xFF;
}

void MCUCommunicator::set_temperature(uint8_t top_temp, uint8_t bottom_temp) {
    this->top_temperature = top_temp;
    this->bottom_temperature = bottom_temp;
}

void MCUCommunicator::set_time(uint8_t hours, uint8_t minutes) {
    this->hours = hours;
    this->minutes = minutes;
}

void MCUCommunicator::set_power(bool power) {
    this->power = power;
}

void MCUCommunicator::set_sleep(bool sleep) {
    this->sleep = sleep;
}

uint8_t MCUCommunicator::get_top_temperature() {
    return top_temperature;
}

uint8_t MCUCommunicator::get_bottom_temperature() {
    return bottom_temperature;
}

void MCUCommunicator::set_led_status(LED_ID led, LED_STATE state) {
    bool state_bool = (state == LED_STATE::ON);
    
    switch (led) {
        case LED_ID::LED1:
            led1_status = state_bool;
            break;
        case LED_ID::LED2:
            led2_status = state_bool;
            break;
        case LED_ID::LED3:
            led3_status = state_bool;
            break;
        case LED_ID::LED4:
            led4_status = state_bool;
            break;
        case LED_ID::LED5:
            led5_status = state_bool;
            break;
        case LED_ID::LED6:
            led6_status = state_bool;
            break;
        case LED_ID::LED7:
            led7_status = state_bool;
            break;
        case LED_ID::LED8:
            led8_status = state_bool;
            break;
        case LED_ID::LED9_ORANGE:
            led9_orange_status = state_bool;
            break;
        case LED_ID::LED9_BLUE:
            led9_blue_status = state_bool;
            break;
        default:
            break;
    }
}

} // namespace ricecooker
} // namespace esphome