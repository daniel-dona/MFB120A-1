#include "program.h"

#include "esphome/core/log.h"
#include "esp_log.h"

namespace esphome {
namespace ricecooker {

    void KeepWarm::step(Heater* heater) {

        auto bottom_temp = heater->get_bottom_temperature();
        auto top_temp = heater->get_top_temperature();

        switch (stage) {

            case Wait:
                ESP_LOGD(TAG, "Keep warm waiting. Temperature: top: %dºC, bottom: %dºC", top_temp, bottom_temp);
                break;

            case Warm:
                ESP_LOGD(TAG, "Keep Warm. Temperatures: top: %dºC, bottom: %dºC, target: %dºC, hysteresis: %dºC",
                    top_temp, bottom_temp, target_temp, hysteresis);

                heater->power_modulate(target_temp, hysteresis);

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

    static const unsigned int RICE_PROGRAM_SOAK_MINUTES = 45;
    static const unsigned int RICE_PROGRAM_REST_MINUTES = 10;

    std::optional<unsigned int> RiceProgram::remaining_time() {

        if (finished)
            return 0;

        unsigned int res = 0;
        
        switch (stage) {
            Wait:
                return std::nullopt;
            Start:
                // Just a guess
                // TODO: calculate time needed to step up the temperature
                res += 2;
            Soak:
                if (!fast)
                    res += RICE_PROGRAM_SOAK_MINUTES;
            Heat:
                if (!fast)
                    res += 2;
                else
                    // Guess more time as fast program starts from lower temperature
                    res += 4;
            Cook:
                res += cooking_time / 2;
            Vapor:
                res += cooking_time / 2;
            Rest:
                if (!fast)
                    res += RICE_PROGRAM_REST_MINUTES;
        }

        res -= (millis() - stage_started) / 1000 / 60;

        return res;
    }

    void RiceProgram::set_stage(Stage stage) {
        this->stage = stage;
        this->stage_started = millis();
    }


    void RiceProgram::step(Heater* heater) {

        auto now = millis();

        uint8_t bottom_temp = heater->get_bottom_temperature();
        uint8_t top_temp = heater->get_top_temperature();

        uint8_t target;

        switch (this->stage) {

            case Wait:

                ESP_LOGD(TAG, "Rice: Waiting, Temperature: top: %dºC, bottom: %dºC",
                    top_temp, bottom_temp);

                heater->power_off();

                break;

            case Start:

                target = 60;

                ESP_LOGD(TAG, "Rice: Soaking, Temperature: top: %dºC, bottom: %dºC, target: %dºC",
                    top_temp, bottom_temp, target);

                heater->power_modulate(target, 0);

                if (heater->get_bottom_temperature() >= target) {
                    set_stage(Soak);
                }

                break;

            case Soak:

                target = 65;

                if (this->fast || now > stage_started + RICE_PROGRAM_SOAK_MINUTES * 60 * 1000) {
                    set_stage(Heat);
                }

                ESP_LOGD(TAG, "Rice: Soaking, Temperature: top: %dºC, bottom: %dºC, target: %dºC",
                    top_temp, bottom_temp, target);

                heater->power_modulate(target, 5);

                break;

            case Heat:

                target = 95;

                ESP_LOGD(TAG, "Rice: Heating, Temperature: top: %dºC, bottom: %dºC, target: %dºC",
                    top_temp, bottom_temp, target);

                heater->power_modulate(target, 2);

                if (heater->get_bottom_temperature() >= target) {
                    set_stage(Cook);
                }

                if (now > stage_started + 30 * 60 * 1000) {
                    // Heating is taking too long, something must be wrong

                    // TODO: display error
                    
                    heater->power_off();
                    finished = true;
                }

                break;

            case Cook:

                target = cooking_temp;
                vapor_max = std::clamp(top_temp, vapor_max, (uint8_t) 100);

                ESP_LOGD(TAG, "Rice: Cooking, Temperature: top: %dºC, bottom: %dºC, target: %dºC",
                    top_temp, bottom_temp, target);

                if (top_temp < vapor_max) {
                    heater->power_on();
                }

                heater->power_modulate(target, 1);   

                if (now > stage_started + this->cooking_time / 2 * 60 * 1000) {
                    heater->power_on();
                    set_stage(Vapor);
                }

                break;

            case Vapor:

                // Temperature curve from `cooking_temp` to 120ºC in `cooking_time / 2` minutes
                target = cooking_temp + (120-cooking_temp) * (now - this->stage_started) / 1000 / 60 / (this->cooking_time/2) ; 

                ESP_LOGD(TAG, "Rice: Vapor, Temperature: top: %dºC, bottom: %dºC, target: %dºC",
                    top_temp, bottom_temp, target);

                heater->power_modulate(target, 0);

                if (now > stage_started + this->cooking_time / 2 * 60 * 1000) {
                    heater->power_off();
                    set_stage(Rest);
                }

                break;

            case Rest:

                target = 65;

                ESP_LOGD(TAG, "Rice: Rest, Temperature: top: %dºC, bottom: %dºC, target: %dºC",
                    top_temp, bottom_temp, target);

                heater->power_modulate(target, 4);

                if (this->fast || now > stage_started + RICE_PROGRAM_REST_MINUTES * 60 * 1000) {
                    finished = true;
                }

                break;
            
        }
    }

}
}
