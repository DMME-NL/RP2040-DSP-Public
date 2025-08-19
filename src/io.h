/* io.h
 * Author: Milan Wendt
 * Date:   2025-08-19
 *
 * Copyright (c) 2025 Milan Wendt
 *
 * This file is part of the RP2040-DSP project.
 *
 * This project (in the current state) is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3 as published by the Free Software Foundation.
 *
 * RP2040 DSP is distributed in the hope that it will
 * be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this project. 
 * If not, see <https://www.gnu.org/licenses/>.
 */

#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/adc.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"
#include "pico/util/queue.h"
#include <stdio.h>

// ============================================================================
// === Pin Configuration ======================================================
// ============================================================================

// Encoder Pins
#define ENCODER_A_PIN  3
#define ENCODER_B_PIN  2

// I2C Configuration
#define I2C_PORT       i2c0
#define SDA_PIN        4
#define SCL_PIN        5

// PCA9555 Address and Registers
#define PCA9555_ADDR         0x20
#define PCA9555_INPUT_PORT0  0x00
#define PCA9555_INPUT_PORT1  0x01
#define PCA9555_OUTPUT_PORT0 0x02
#define PCA9555_OUTPUT_PORT1 0x03
#define PCA9555_INT_GPIO     29    // Connected to INT pin of PCA9555

// Multiplexer & Potentiometer Pins
#define MUX_SEL_A      27
#define MUX_SEL_B      26
#define MUX_SEL_C      15
#define ADC_INPUT_PIN  28

// ============================================================================
// === Configuration Constants ================================================
// ============================================================================

#define DEBOUNCE_US             10000   // 30ms debounce 
#define POT_THRESHOLD           16      // 0.39% : 12-bit value 256 steps  
#define ADC_AVERAGE_SAMPLES     64      // Averaging pot read
#define EMA_ALPHA               0.5f    // Smoothing for pot
#define NUM_POTS                8       // Numeber of total potentiometers
#define POT_MAX                 4095    // Max pot value (12 bit)

// ============================================================================
// === Global State ===========================================================
// ============================================================================

// Encoder state
volatile int8_t encoder_position = 1;
static int8_t encoder_step_accumulator = 0;
static uint8_t prev_state = 0;

// PCA9555 input state
uint8_t input_port0 = 0, input_port1 = 0;
volatile bool pca9555_interrupt_flag = false;
uint8_t footswitch_state = 0xF;  // IO0_0 to IO0_3
uint8_t dipswitch_state = 0xF;   // IO0_4 to IO0_7
bool encoder_button = false;     // IO1_4
uint8_t led_state = 0;           // IO1_0 to IO1_3 
uint8_t prev_led_state = 0;
bool lfo_led_state = false;      // IO1_7

// Potentiometer values
uint16_t pot_value[NUM_POTS];
static float pot_ema[NUM_POTS];
static bool initialized = false;
int last_changed_pot = -1;

// ============================================================================
// === Timers and Debounce ====================================================
// ============================================================================

// Global tap interval in milliseconds, initially 0 (no tempo)
volatile uint32_t tap_interval_ms = 500;

// ============================================================================
// === I2C Initialization =====================================================
// ============================================================================

void I2C_Initialize(uint16_t clock_speed_khz) {
    i2c_init(I2C_PORT, clock_speed_khz * 1000); // baudrate is double the clock speed for I2C
    gpio_set_function(SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(SDA_PIN);
    gpio_pull_up(SCL_PIN);
}

// ============================================================================
// === PCA9555 GPIO Expander Setup and Reading ================================
// ============================================================================

void initialize_gpio_expander(void) {
    // Set Port 0 (P0_0 to P0_7) as inputs
    uint8_t config_port0[] = { 0x06, 0xFF };
    i2c_write_blocking(I2C_PORT, PCA9555_ADDR, config_port0, 2, false);

    // Set Port 1:
    // P1_0 to P1_3 and P1_7 as outputs (0)
    // P1_4 to P1_6 as inputs (1)
    uint8_t config_port1[] = { 0x07, 0b01110000 };
    i2c_write_blocking(I2C_PORT, PCA9555_ADDR, config_port1, 2, false);

    // Turn off all LEDs (IO1_0 to IO1_3 and IO1_7) by default
    // Make sure global state variables are initialized accordingly

    // Initialization: all LEDs OFF (bits set to 1)
    led_state = DEFAULT_LED_STATE;
    lfo_led_state = true;  // If needed

    // Turn off all LEDs on expander
    uint8_t initial_out[] = { PCA9555_OUTPUT_PORT1, led_state };
    i2c_write_blocking(I2C_PORT, PCA9555_ADDR, initial_out, 2, false);
}

void update_gpio_expander_state(void) {
    input_port0 = 0, input_port1 = 0;

    // Read and invert input port 0
    uint8_t reg = PCA9555_INPUT_PORT0;
    i2c_write_blocking(I2C_PORT, PCA9555_ADDR, &reg, 1, true);
    i2c_read_blocking(I2C_PORT, PCA9555_ADDR, &input_port0, 1, false);
    input_port0 = ~input_port0;

    // Read and invert input port 1
    reg = PCA9555_INPUT_PORT1;
    i2c_write_blocking(I2C_PORT, PCA9555_ADDR, &reg, 1, true);
    i2c_read_blocking(I2C_PORT, PCA9555_ADDR, &input_port1, 1, false);
    input_port1 = ~input_port1;

    // Parse input states
    footswitch_state  = input_port0 & 0x0F;
    dipswitch_state   = (input_port0 >> 4) & 0x0F;
    encoder_button    = (input_port1 >> 4) & 0x01;
}

// ============================================================================
// === Rotary Encoder Handling ================================================
// ============================================================================

// Encoder transition lookup table
const int8_t transition_table[4][4] = {
    {  0, -1,  1,  0 },
    {  1,  0,  0, -1 },
    { -1,  0,  0,  1 },
    {  0,  1, -1,  0 }
};

void encoder_callback(uint gpio, uint32_t events) {
    uint8_t a = gpio_get(ENCODER_A_PIN);
    uint8_t b = gpio_get(ENCODER_B_PIN);
    uint8_t new_state = (a << 1) | b;

    int8_t delta = transition_table[prev_state][new_state];
    encoder_step_accumulator += delta;
    prev_state = new_state;

    if (encoder_step_accumulator >= 3) {
        encoder_position++;
        encoder_step_accumulator = -1;
    } else if (encoder_step_accumulator <= -4) {
        encoder_position--;
        encoder_step_accumulator = 0;
    }
}

void setup_encoder(void) {
    gpio_init(ENCODER_A_PIN); gpio_set_dir(ENCODER_A_PIN, GPIO_IN); gpio_pull_up(ENCODER_A_PIN);
    gpio_init(ENCODER_B_PIN); gpio_set_dir(ENCODER_B_PIN, GPIO_IN); gpio_pull_up(ENCODER_B_PIN);

    prev_state = (gpio_get(ENCODER_A_PIN) << 1) | gpio_get(ENCODER_B_PIN);

    gpio_set_irq_enabled(ENCODER_A_PIN, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);
    gpio_set_irq_enabled(ENCODER_B_PIN, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);
}

// ============================================================================
// === PCA9555 Interrupt Setup ================================================
// ============================================================================

void gpio29_irq_handler(uint gpio, uint32_t events) {
    pca9555_interrupt_flag = true;
}

void setup_pca9555_interrupt(void) {
    gpio_init(PCA9555_INT_GPIO);
    gpio_set_dir(PCA9555_INT_GPIO, GPIO_IN);
    gpio_pull_up(PCA9555_INT_GPIO);

    // No direct irq handler here — all goes through global handler
    gpio_set_irq_enabled(PCA9555_INT_GPIO, GPIO_IRQ_EDGE_FALL, true);
}

// ============================================================================
// === Unified GPIO Interrupt Handler =========================================
// ============================================================================

void global_gpio_irq_handler(uint gpio, uint32_t events) {
    if (gpio == ENCODER_A_PIN || gpio == ENCODER_B_PIN) {
        encoder_callback(gpio, events);
    } else if (gpio == PCA9555_INT_GPIO) {
        gpio29_irq_handler(gpio, events);
    }
}

void setup_global_irq_handler(void) {
    gpio_set_irq_callback(global_gpio_irq_handler);
    irq_set_enabled(IO_IRQ_BANK0, true);
}

// ============================================================================
// === Potentiometer Handling (via 4051 MUX) ==================================
// ============================================================================

void initialize_potentiometers(void) {
    adc_init();
    adc_gpio_init(ADC_INPUT_PIN);
    adc_select_input(2);

    gpio_init(MUX_SEL_A); gpio_set_dir(MUX_SEL_A, GPIO_OUT);
    gpio_init(MUX_SEL_B); gpio_set_dir(MUX_SEL_B, GPIO_OUT);
    gpio_init(MUX_SEL_C); gpio_set_dir(MUX_SEL_C, GPIO_OUT);
}

void set_mux_channel(uint8_t channel) {
    gpio_put(MUX_SEL_A, channel & 0x01);
    gpio_put(MUX_SEL_B, (channel >> 1) & 0x01);
    gpio_put(MUX_SEL_C, (channel >> 2) & 0x01);
    sleep_us(5);  // MUX settle time
}

// Wiring for the 4051 MUX:
const uint8_t pot_mux_map[NUM_POTS] = {4, 6, 7, 1, 0, 3, 2, 5};

int read_all_pots(bool force) {
    int changed_pot_index = -1;

    for (uint8_t i = 0; i < NUM_POTS; ++i) {
        set_mux_channel(pot_mux_map[i]);  // Use remapped MUX channel
        sleep_us(50);

        uint32_t total = 0;
        for (int s = 0; s < ADC_AVERAGE_SAMPLES; ++s) {
            total += adc_read();
        }

        uint16_t average = total / ADC_AVERAGE_SAMPLES;

        if (!initialized) {
            pot_ema[i] = average;
            pot_value[i] = average;
        }

        pot_ema[i] = EMA_ALPHA * average + (1.0f - EMA_ALPHA) * pot_ema[i];
        uint16_t new_value = (uint16_t)(pot_ema[i] + 0.5f);

        if (new_value > pot_value[i] + POT_THRESHOLD || new_value < pot_value[i] - POT_THRESHOLD) {
            pot_value[i] = new_value;
            changed_pot_index = i;
            if (PRINT_POT_VALUE && DEBUG) printf("Pot %d: %d\n", i, new_value);
        } else if (force) {
            pot_value[i] = new_value;
        }
    }

    if (!initialized) initialized = true;
    return changed_pot_index;
}

// ============================================================================
// === Tap Tempo logic ========================================================
// ============================================================================

static absolute_time_t next_blink_time = {0};
static bool blink_state = false;
static absolute_time_t last_tap_time = {0};
static bool tap_started = false;


void update_tap_blink(void) {
    if (tap_interval_ms == 0) return; // no tempo set yet

    absolute_time_t now = get_absolute_time();
    if (absolute_time_diff_us(now, next_blink_time) <= 0) {
        blink_state = !blink_state;
        if (blink_state)
            led_state |= (1 << 3);  // LED 3 on
        else
            led_state &= ~(1 << 3); // LED 3 off

        uint8_t out[2] = { PCA9555_OUTPUT_PORT1, (lfo_led_state << 7) | (led_state & 0x0F) };
        i2c_write_blocking(I2C_PORT, PCA9555_ADDR, out, 2, false);

        next_blink_time = delayed_by_ms(now, tap_interval_ms / 2);
    }
}

// Call this every time the tap tempo footswitch (footswitch 4) is pressed:
void on_tap_press(void) {
    absolute_time_t now = get_absolute_time();

    if (tap_started) {
        uint32_t interval = to_ms_since_boot(now) - to_ms_since_boot(last_tap_time);
        if (interval > 50 && interval < 2000) {  // Ignore too-fast or too-slow taps
            tap_interval_ms = interval;
        }
    } else {
        tap_started = true;
    }

    last_tap_time = now;

    // Reset blinking with new tempo
    next_blink_time = delayed_by_ms(now, tap_interval_ms / 2);
    blink_state = true;
    led_state |= (1 << 3);

    uint8_t out[2] = { PCA9555_OUTPUT_PORT1, led_state };
    i2c_write_blocking(I2C_PORT, PCA9555_ADDR, out, 2, false);
}

// ============================================================================
// === Footswitch Logic =======================================================
// ============================================================================

static uint8_t prev_footswitch_state = 0;

uint8_t handle_footswitches(void) {
    uint8_t changed = (footswitch_state ^ prev_footswitch_state) & footswitch_state; // detect new presses
    prev_footswitch_state = footswitch_state;

    if (changed & 0x01) led_state ^= (1 << 1); // Footswitch 1 -> LED 0 toggle
    if (changed & 0x02) led_state ^= (1 << 0); // Footswitch 2 -> LED 1 toggle
    if (changed & 0x04) led_state ^= (1 << 2); // Footswitch 3 -> LED 2 toggle

    if (changed & 0x08) { // Footswitch 4 (tap tempo)
       on_tap_press();
    }

    // Always return, even when turning off LEDs
    if ((changed & 0x01) /*&& (led_state & (1 << 1))*/)     { return 2; } // Footswitch 1 changed
    else if ((changed & 0x02) /*&& (led_state & (1 << 0))*/){ return 1; } // Footswitch 2 changed
    else if ((changed & 0x04) /*&& (led_state & (1 << 2))*/){ return 3; } // Footswitch 3 change
    else{   return 0; } // No change
}