#include "program.h"

#include "esphome/core/log.h"
#include "esp_log.h"

namespace esphome {
namespace ricecooker {

    void KeepWarm::step(RiceCooker* ricecooker) {

        auto bottom_temp = ricecooker->get_bottom_temperature();
        auto top_temp = ricecooker->get_top_temperature();

        switch (stage) {

            case Wait:
                ESP_LOGD(TAG, "Keep warm waiting. Temperature: top: %dºC, bottom: %dºC", top_temp, bottom_temp);
                break;

            case Warm:
                ESP_LOGD(TAG, "Keep Warm. Temperatures: top: %dºC, bottom: %dºC, target: %dºC, hysteresis: %dºC",
                    top_temp, bottom_temp, target_temp, hysteresis);

                ricecooker->power_modulate(target_temp, hysteresis);

                break;
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

        uint8_t bottom_temp = ricecooker->get_bottom_temperature();
        uint8_t top_temp = ricecooker->get_top_temperature();

        uint8_t target;

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

                ricecooker->power_modulate(target, 0);

                if (ricecooker->get_bottom_temperature() >= target) {
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

                ricecooker->power_modulate(target, 5);

                break;

            case Heat:

                target = 95;

                ESP_LOGD(TAG, "Rice: Heating, Temperature: top: %dºC, bottom: %dºC, target: %dºC",
                    top_temp, bottom_temp, target);

                ricecooker->power_modulate(target, 2);

                if (ricecooker->get_bottom_temperature() >= target) {
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
                vapor_max = std::clamp(top_temp, vapor_max, (uint8_t) 100);

                ESP_LOGD(TAG, "Rice: Cooking, Temperature: top: %dºC, bottom: %dºC, target: %dºC",
                    top_temp, bottom_temp, target);

                if (top_temp < vapor_max) {
                    ricecooker->power_on();
                }

                ricecooker->power_modulate(target, 1);   

                if (now > stage_started + this->cooking_time / 2 * 60 * 1000) {
                    ricecooker->power_on();
                    set_stage(Vapor);
                }

                break;

            case Vapor:

                // Temperature curve from `cooking_temp` to 120ºC in `cooking_time / 2` minutes
                target = cooking_temp + (120-cooking_temp) * (now - this->stage_started) / 1000 / 60 / (this->cooking_time/2) ; 

                ESP_LOGD(TAG, "Rice: Vapor, Temperature: top: %dºC, bottom: %dºC, target: %dºC",
                    top_temp, bottom_temp, target);

                ricecooker->power_modulate(target, 0);

                if (now > stage_started + this->cooking_time / 2 * 60 * 1000) {
                    ricecooker->power_off();
                    set_stage(Rest);
                }

                break;

            case Rest:

                target = 65;

                ESP_LOGD(TAG, "Rice: Rest, Temperature: top: %dºC, bottom: %dºC, target: %dºC",
                    top_temp, bottom_temp, target);

                ricecooker->power_modulate(target, 4);

                if (this->fast || now > stage_started + 10 * 60 * 1000) {
                    ricecooker->power_off();
                    ricecooker->set_program(new KeepWarm(65, 2));
                    ricecooker->start();
                }

                break;
            
        }
    }

}
}
