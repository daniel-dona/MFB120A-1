#include "ricecooker.h"
#include "esphome/core/log.h"

namespace esphome {
namespace ricecooker {

    RiceCooker::RiceCooker() : Component(), UARTDevice() {}

    // Control

    void RiceCooker::power_on(){
        heater.power_on();
        mcu_communicator->set_power(true);
    }

    void RiceCooker::power_off(){
        heater.power_off();
        mcu_communicator->set_power(false);
    }

    uint8_t RiceCooker::get_top_temperature(){
        return mcu_communicator->get_top_temperature();
    }

    uint8_t RiceCooker::get_bottom_temperature(){
        return mcu_communicator->get_bottom_temperature();
    }

    bool RiceCooker::get_power(){
        return heater.get_power();
    }

    void RiceCooker::set_program(Program* program){

        if (program == nullptr) {
            ESP_LOGD(TAG, "Setting Program: null");
        } else {
            ESP_LOGD(TAG, "Setting Program: %s", program->get_name());
        }

        heater.reset();

        if (this->program != nullptr) {
            delete this->program;
        }
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
        this->heater.reset();

        if (this->program != nullptr)
            program->cancel();
    }

    void RiceCooker::timer(){
        minutes++;

        if(minutes == 60){
            hours = ++hours % 100;
            minutes = 0;
        }
    }

    void RiceCooker::setup() {
        // Initialize MCU communicator
        mcu_communicator = new MCUCommunicator(this);
        mcu_communicator->setup();
    }

    void RiceCooker::loop() {
        // Update MCU communication
        mcu_communicator->loop();

        // Update heater with latest temperature data
        uint8_t top_temp = mcu_communicator->get_top_temperature();
        uint8_t bottom_temp = mcu_communicator->get_bottom_temperature();

        heater.update(top_temp, bottom_temp);

        if (millis() > relay_last + relay_interval){
            relay_last = millis();

            if (this->program != nullptr) {
                this->program->step(&heater);
                heater.step(millis());

                // Extra safety: check remaining_time() returns valid value
                std::optional<unsigned int> remaining = this->program->remaining_time();
                if (remaining.has_value() && *remaining <= 0) {
                    heater.power_off();
                    set_program(new KeepWarm(65, 2));
                    start();
                }
            } else {
                ESP_LOGD(TAG, "No program selected");
            }
        }

        // Update display time based on program or temperature
        if (this->program != nullptr) {
            std::optional<unsigned int> remaining = this->program->remaining_time();
            if (remaining.has_value()) {
                this->hours = *remaining / 60;
                this->minutes = *remaining % 60;
            } else {
                this->hours = top_temp;
                this->minutes = bottom_temp;
            }
        } else {
            this->hours = top_temp;
            this->minutes = bottom_temp;
        }

        // Update MCU display
        mcu_communicator->set_time(this->hours, this->minutes);
        mcu_communicator->set_power(heater.get_power());
        mcu_communicator->set_sleep(this->sleep);
    }

}
}
