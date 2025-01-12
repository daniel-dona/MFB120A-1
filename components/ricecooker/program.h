#pragma once

#include "ricecooker.h"

namespace esphome {
namespace ricecooker {

class RiceCooker;

static char fast_rice_name[] = "Fast Rice";
static char rice_name[] = "Rice";
static char keepwarm_name[] = "Keep Warm";
static char none_name[] = "None";

class Program {
    public:
        virtual void step(RiceCooker* ricecooker) = 0;
        virtual char* get_name() = 0;

        /*
            Starts the program.

            If the program was previously cancelled,
            starting it will start from the beginning, not the last state.
        */
        virtual void start() = 0;

        virtual void cancel() = 0;
};

class KeepWarm : public Program {
    public:
        void step(RiceCooker* ricecooker) override;
        char* get_name() override;
        void start() override;
        void cancel() override;

        KeepWarm(uint8_t target_temp, uint8_t hysteresis);

    private:
        uint8_t target_temp;
        uint8_t hysteresis;

        enum Stage { Wait, Warm } stage = Wait;
};

class RiceProgram : public Program {
    public:
        void step(RiceCooker* ricecooker) override;
        char* get_name() override;
        void start() override;
        void cancel() override;

        RiceProgram(uint8_t cooking_time);
        RiceProgram(uint8_t cooking_time, uint8_t cooking_temp);
        RiceProgram(uint8_t cooking_time, bool fast);
        RiceProgram(uint8_t cooking_time, uint8_t cooking_temp, bool fast);

    private:
        // Config
        uint8_t cooking_time;
        uint8_t cooking_temp = 100;
        bool fast = false;

        // State
        enum Stage { Wait, Start, Soak, Heat, Cook, Vapor, Rest } stage = Wait;
        int stage_started;

        void set_stage(Stage stage);

        uint8_t vapor_max = 0;
};

}
}
