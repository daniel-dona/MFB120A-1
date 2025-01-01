#pragma once

#include "esphome/core/component.h"
#include "esphome/core/datatypes.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/sensor/sensor.h"

namespace esphome {
namespace ricecooker {

static const uint8_t RECV_HEADER = 0xaa;
static const uint8_t SEND_HEADER = 0x55;

static char fast_rice_name[] = "Fast Rice";
static char rice_name[] = "Rice";
static char keepwarm_name[] = "Keep Warm";
static char none_name[] = "None";

class RiceCooker;

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
        uint8_t cooking_temp = 95;
        bool fast = false;

        // State
        enum Stage { Wait, Soak, Heat, Cook, Vapor } stage = Wait;
        int stage_started;

        void set_stage(Stage stage);
};

class RiceCooker : public Component, public uart::UARTDevice {

    public:
        void start();
        void cancel();
        void set_cooking_mode();
        void manual_temperature_set(uint8_t temp);
        void manual_timer_set();  

        void power_on();
        void power_off();
        void power_module(uint8_t target_temp, uint8_t hysteresis);

        void set_program(Program* program);

        uint8_t get_top_temperature();
        uint8_t get_bottom_temperature();

        bool get_power();

        char* get_program_name();

        void setup() override;
        void loop() override;
        void update();
        //void dump_config() override;

    private:

        uint16_t crc16(const uint8_t *data, size_t len);
        uint8_t int_7seg(uint8_t value, bool dot);
        
        void write_data(uint8_t *buffer);
        void timer();
        void mcu_send();
        void mcu_recv();

        // UART communication with MCU
        uint8_t send_buffer[11];
        uint8_t recv_buffer[10];

        // Tickers
        int mcu_interval = 100;
        int relay_interval = 2000;
        int mcu_last = 0, relay_last = 0;

        // State
        int hours = 0;
        int minutes = 0;
        bool middle_dots = true;

        bool power = false;

        bool sleep = false;

        Program* program {nullptr};

        uint8_t top_temperature;
        uint8_t bottom_temperature;
};


}
}