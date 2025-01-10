#include "ricecooker.h"

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"

#include "esphome/core/log.h"
#include "esp_log.h"


namespace esphome {
namespace ricecooker {

    uint16_t RiceCooker::crc16(const uint8_t *data, size_t len) {
        uint16_t crc = 0x0000;  // Initialize CRC

        while (len--) {
            //ESP_LOGD("crc data", "%d %d ", len, *data);

            crc ^= (*data++) << 8; // XOR data byte with CRC, and shift left to treat as most significant byte
            for (int i = 0; i < 8; i++) {
                if (crc & 0x8000) { // Check MOST significant bit
                    crc = (crc << 1) ^ 0x1021; // Left shift and XOR polynomial
                } else {
                    crc <<= 1; // Left shift
                }
                //ESP_LOGD("crc shift", "%d", crc);
            }
            //ESP_LOGD("crc", "%d", crc);
        }
        return crc;
    }

    uint8_t RiceCooker::int_7seg(uint8_t value, bool dot){

        //ESP_LOGD("int_7seg", "%d", value);

        uint8_t table [] = {
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

    // Control

    void RiceCooker::power_on(){
        if(!this->power){
            ESP_LOGD(TAG, "Heater power: on");
            this->power = true;
        }
    }

    void RiceCooker::power_off(){
        if(this->power){
            ESP_LOGD(TAG, "Heater power: off");
            this->power = false;
        }
    }

    /*
    */
    void RiceCooker::power_modulate(uint8_t target_temp, uint8_t hysteresis) {

        if (power_remain != 0) {
            power_remain = std::max(1, power_remain - (int) (millis() - power_modulate_last) );
        }
        
        power_wait_remain = std::max(0, power_wait_remain - (int) (millis() - power_modulate_last) );
        
        power_modulate_last = millis();

        auto min_target = target_temp - hysteresis;
        auto max_target = target_temp + hysteresis;

        if (bottom_temperature < min_target && power_remain == 0 && power_wait_remain == 0) {

            power_on();

            int diff = (int) last_max_target - (int) max_temperature;
            diff = std::clamp(diff, -3, 3);
            ESP_LOGD(TAG, "----- Found a diff of %dºC", diff);
            power_speed = std::clamp(power_speed + 200 * diff, 100, 8000);

            power_remain = (max_target - bottom_temperature) * power_speed;
            relay_interval = std::clamp(power_remain, 100, 5000);

            last_max_target = max_target;
            max_temperature = bottom_temperature;

            ESP_LOGD(TAG, "Power modulating: heating ON for %d ms, Power speed %d", power_remain, power_speed);

        } else if (bottom_temperature >= max_target || power_remain == 1) {

            power_off();

            power_remain = 0;
            relay_interval = 2000;
            power_wait_remain = 30000;

        } else {
            // Keep last heating state to reduce relay wear.

            ESP_LOGD(TAG, "Power modulating: power remaining %d ms, power waiting %d ms, Power speed %d", power_remain, power_wait_remain, power_speed);
        }
    }

    uint8_t RiceCooker::get_top_temperature(){
        return recv_buffer[3];
    }

    uint8_t RiceCooker::get_bottom_temperature(){
        return recv_buffer[4];
    }

    bool RiceCooker::get_power(){
        return this->power;
    }

    void RiceCooker::set_program(Program* program){

        if (program == nullptr) {
            ESP_LOGD(TAG, "Setting Program: null");
            this->power_off();
        } else {
            ESP_LOGD(TAG, "Setting Program: %s", program->get_name());
        }

        last_max_target = 0;
        max_temperature = 0;

        delete this->program;
        this->program = program;
    }

    char* RiceCooker::get_program_name() {
        if (this->program != nullptr) {
            return this->program->get_name();
        } else {
            return none_name;
        }
            
    };

    void RiceCooker::start() {
        if (this->program != nullptr)
            program->start();
    }

    void RiceCooker::cancel() {
        this->power_off();

        if (this->program != nullptr)
            program->cancel();
    }

    void RiceCooker::write_data(uint8_t *buffer){

        buffer[0] = 0x55; // Header
        buffer[1] = 0x07; //?

        buffer[2] = 0b00000000;

        buffer[2] |= 0b00000001; // Some kind of general ON?
        //buffer[2] |= 0b00010000; // Beep
        //buffer[2] |= 0b00000100; // RL


        if(power){
            buffer[2] |= 0b00000100;
        }

        if (sleep){
            buffer[2] |= 0b00100000; // Sleep
        }

        buffer[3] = int_7seg(hours/10, false);
        buffer[4] = int_7seg(hours%10, true);
        buffer[5] = int_7seg(minutes/10, true);
        buffer[6] = int_7seg(minutes%10, false);

        buffer[7] = 0b00000000;

        if (this->power){

            buffer[7] |= 0b00000001; // LED1
        }
        //buffer[7] |= 0b00000010; // LED2
        //buffer[7] |= 0b00000100; // LED3
        //buffer[7] |= 0b00001000; // LED4
        //buffer[7] |= 0b00010000; // LED5


        buffer[8] = 0b00000000;

        //buffer[8] |= 0b00000001; // LED 6
        //buffer[8] |= 0b00000010; // LED 7
        //buffer[8] |= 0b00000100; // LED 8

        //buffer[8] |= 0b00001000; // LED9 orange
        //buffer[8] |= 0b00010000; // LED9 blue


        uint16_t crc = crc16(buffer+sizeof(uint8_t), 8);

        buffer[9] = (crc >> 8) & 0xFF;
        buffer[10] = crc & 0xFF;

        //ESP_LOGD("rice_crc", buffer[9]);
        //ESP_LOGD("rice_crc", buffer[10]);

    }

    void RiceCooker::timer(){

        minutes++;

        if(minutes == 60){
            hours = ++hours % 100;
            minutes = 0;
        }

    }

    void RiceCooker::mcu_send(){

        this->write_data(send_buffer);
        this->write_array(send_buffer, 11);

    }

    void RiceCooker::mcu_recv(){

        uint8_t ch;
        uint8_t n = 0;

        while (this->available()) {

            ch = this->read();

            if (ch == RECV_HEADER || n == 10){
                n = 0;
            }
            
            recv_buffer[n] = ch;

            n++;
            
        }

        uint16_t crc = crc16(recv_buffer+sizeof(uint8_t), 7);

        if (recv_buffer[0] != RECV_HEADER || recv_buffer[8] != ((crc >> 8) & 0xFF) || recv_buffer[9] != (crc & 0xFF)){
            ESP_LOGD(TAG, "Got an invalid package from MCU");
            return;
        }

        this->top_temperature = recv_buffer[3];
        this->bottom_temperature = recv_buffer[4];
        this->max_temperature = std::max(this->max_temperature, recv_buffer[4]);
    }


    void RiceCooker::setup() {

        uint8_t reset[][11] = {
        {0x55,0x07,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xc7,0x18}, //Black
        {0x55,0x07,0x10,0x00,0x00,0x00,0x00,0x1f,0x1f,0x00,0xf0}, //Beep
        {0x55,0x07,0x10,0xff,0xff,0xff,0xff,0x1f,0x1f,0x8a,0x20}, //Beep
        {0x55,0x07,0x00,0xff,0xff,0xff,0xff,0x1f,0x1f,0xbd,0x5b}, //All
        {0x55,0x07,0x00,0xff,0xff,0xff,0xff,0x02,0x18,0xb8,0x93}  //?
        };

        //register_service(&RiceCooker::send_command, "send_command", {"command"});

        write_array(reset[0], 11);
        vTaskDelay(50);
        write_array(reset[1], 11);
        vTaskDelay(50);
        write_array(reset[2], 11);
        vTaskDelay(50);
        write_array(reset[3], 11);
        vTaskDelay(50);
        write_array(reset[0], 11);
        vTaskDelay(50);

    }

    void RiceCooker::loop() {

        if (millis() > mcu_last + mcu_interval){

            mcu_last = millis();

            //ESP_LOGI("tick", "%d", last);

            this->mcu_send();
            this->mcu_recv();

            this->hours = get_top_temperature();
            this->minutes = get_bottom_temperature();
        }

        if (millis() > relay_last + relay_interval){

            relay_last = millis();

            if (this->program != nullptr) {
                this->program->step(this);
            } else {
                ESP_LOGD(TAG, "No program selected");
            }
            
        }

    }


}
}
