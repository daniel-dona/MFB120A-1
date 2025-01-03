#include "ricecooker.h"

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"

#include "esphome/core/log.h"
#include "esp_log.h"



namespace esphome {
namespace ricecooker {

    static const char *const TAG = "ricecooker";

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
    void RiceCooker::power_module(uint8_t target_temp, uint8_t hysteresis) {

        int lag;
        if (max_temperature > last_max_target) {
            lag = max_temperature - last_max_target;
        } else {
            lag = 0;
        }
        ESP_LOGD(TAG, "Computed a temperature sensor lag of %dºC", lag);

        auto min_target = target_temp - hysteresis;
        auto max_target = std::max(min_target, target_temp + hysteresis - lag);

        if (bottom_temperature < min_target) {
            power_on();
        } else if (bottom_temperature >= max_target) {
            power_off();
            last_max_target = max_target;
            max_temperature = bottom_temperature;
        } else {
            // Keep last heating state to reduce relay wear.
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

    void KeepWarm::step(RiceCooker* ricecooker) {

        auto bottom_temp = ricecooker->get_bottom_temperature();
        auto top_temp = ricecooker->get_top_temperature();

        switch (stage) {

            case Wait:
                ESP_LOGD(TAG, "Keep warm waiting. Temperature: top: %dºC, bottom: %dºC", top_temp, bottom_temp);
                break;

            case Warm:
                ESP_LOGD(TAG, "Temperature: top: %dºC, bottom: %dºC, target: %dºC, hysteresis: %dºC",
                    top_temp, bottom_temp, target_temp, hysteresis);

                ricecooker->power_module(target_temp, hysteresis);
        }
    }

    KeepWarm::KeepWarm(uint8_t target_temp, uint8_t hysteresis)
        : target_temp(target_temp)
        , hysteresis(hysteresis)
    {}

    char* KeepWarm::get_name() {
        return keepwarm_name;
    }

    void KeepWarm::start() {
        this->stage = Warm;
    }

    void KeepWarm::cancel() {
        this->stage = Wait;
    }

    RiceProgram::RiceProgram(uint8_t cooking_time)
        : stage_started(millis())
        , cooking_time(cooking_time)
    {}

    RiceProgram::RiceProgram(uint8_t cooking_time, uint8_t cooking_temp)
        : stage_started(millis())
        , cooking_time(cooking_time)
        , cooking_temp(cooking_temp)
    {}

    RiceProgram::RiceProgram(uint8_t cooking_time, bool fast)
        : stage_started(millis())
        , cooking_time(cooking_time)
        , fast(fast)
    {}

    RiceProgram::RiceProgram(uint8_t cooking_time, uint8_t cooking_temp, bool fast)
        : stage_started(millis())
        , cooking_time(cooking_time)
        , cooking_temp(cooking_temp)
        , fast(fast)
    {}

    char* RiceProgram::get_name() {

        if (fast) {
            return fast_rice_name;
        } else {
            return rice_name;
        }
    }


    void RiceProgram::start() {
        set_stage(Start);
    }

    void RiceProgram::cancel() {
        set_stage(Wait);
    }

    void RiceProgram::set_stage(Stage stage) {
        this->stage = stage;
        this->stage_started = millis();
    }


    void RiceProgram::step(RiceCooker* ricecooker) {

        auto now = millis();

        auto bottom_temp = ricecooker->get_bottom_temperature();
        auto top_temp = ricecooker->get_top_temperature();

        int target;

        switch (this->stage) {

            case Wait:

                ESP_LOGD(TAG, "Rice: Waiting, Temperature: top: %dºC, bottom: %dºC",
                    top_temp, bottom_temp);

                ricecooker->power_off();

                break;

            case Start:

                target = 60;

                ESP_LOGD(TAG, "Rice: Soaking, Temperature: top: %dºC, bottom: %dºC, target: %dºC",
                    top_temp, bottom_temp, target);

                if (ricecooker->get_bottom_temperature() < target) {
                    ricecooker->power_module(target, 0);
                } else {
                    set_stage(Soak);
                }

                break;

            case Soak:

                target = 65;

                if (now > stage_started + 45 * 60 * 1000 || this->fast) {
                    set_stage(Heat);
                }

                ESP_LOGD(TAG, "Rice: Soaking, Temperature: top: %dºC, bottom: %dºC, target: %dºC",
                    top_temp, bottom_temp, target);

                ricecooker->power_module(target, 5);

                break;

            case Heat:

                target = 97;

                ESP_LOGD(TAG, "Rice: Heating, Temperature: top: %dºC, bottom: %dºC, target: %dºC",
                    top_temp, bottom_temp, target);

                if (ricecooker->get_bottom_temperature() < target) {
                    ricecooker->power_module(target, 0);
                } else {
                    set_stage(Cook);
                }

                if (now > stage_started + 30 * 60 * 1000) {
                    // Heating is taking too long, something must be wrong

                    // TODO: display error
                    
                    ricecooker->power_off();
                    ricecooker->set_program(nullptr);
                }

                break;

            case Cook:

                target = cooking_temp;

                ESP_LOGD(TAG, "Rice: Cooking, Temperature: top: %dºC, bottom: %dºC, target: %dºC",
                    top_temp, bottom_temp, target);

                ricecooker->power_module(target, 1);

                if (now > stage_started + this->cooking_time / 2 * 60 * 1000) {
                    ricecooker->power_on();
                    set_stage(Vapor);
                }

                break;

            case Vapor:

                // Temperature curve from `cooking_temp` to 120ºC in `cooking_time / 2` minutes
                target = cooking_temp + (120-cooking_temp) * (now - this->stage_started) / 1000 / 60 / this->cooking_time / 2 ; 

                ESP_LOGD(TAG, "Rice: Vapor, Temperature: top: %dºC, bottom: %dºC, target: %dºC",
                    top_temp, bottom_temp, target);

                ricecooker->power_module(target, 0);

                if (now > stage_started + this->cooking_time / 2 * 60 * 1000) {
                    ricecooker->power_off();
                    ricecooker->set_program(new KeepWarm(65, 2));
                    ricecooker->start();
                }

                break;
            
        }
    }
}
}