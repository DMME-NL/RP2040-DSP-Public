/* 
 * Author: Milan Wendt
 * Date:   2025-09-03
 *
 * Copyright (c) 2025 Milan Wendt
 *
 * This file is part of the RP2040-DSP project.
 *
 * This project is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3 as published by the Free Software Foundation.
 *
 * RP2040 DSP is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; 
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this project. 
 * If not, see <https://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <stdbool.h>

// Pico SDK headers
#include "pico/stdlib.h"
#include "pico/binary_info.h"
#include "pico/time.h"
#include "pico/multicore.h"

// Include hardware headers
#include "hardware/dma.h"
#include "hardware/clocks.h"
#include "hardware/structs/timer.h"

// SSD1306 OLED display headers
#include "font.h"
#include "ssd1306.h"

// Include I2S library for audio processing
#include "i2s.h"

// Include SPI ram
#include "spi_ram.h"

// ============================================================================
// === TIming & Debugging =====================================================
// ============================================================================

// These do not represent real frequencies
// Overclocking system clock causes strange behavior 
// === CONFIGURATION CONSTANTS ===
#define SYSTEM_CLOCK_MHZ     250   // 250 MHz system clock
#define PERIPHERAL_CLOCK_MHZ 125   // 100 MHz peripheral clock
#define SPI_TARGET_HZ         42   // 42 MHz
#define I2C_TARGET_HZ       1000   // 1000 kHz

// Enable debugging features
#define SHOW_CPU            1  // Show CPU usage in the UI
#define DEBUG               1  // Enable debug mode for additional logging
#define PRINT_POT_VALUE     0  // Print POTS values to console (on change)
#define PRINT_IO            0  // Print GPIO values to console (on change)
#define PRINT_ACTION        0  // Print actions to console

#define PRINT_CPU           0  // Print CPU usage       in DEBUG
#define PRINT_RAM           0  // Print RAM usage       in DEBUG
#define PRINT_FLASH         0  // Print FLASH usage     in DEBUG
#define PRINT_EFFECTS       0  // Print EFEFCTS status  in DEBUG
#define PRINT_CLOCK         0  // Print CLOCK INFO info in DEBUG
#define PRINT_I2S           0  // Print I2S debug info  in DEBUG

// When no hardware is connected, we can use the default LED state
uint8_t default_led_state = 0x01;

// Process gain and compression in stereo?
// This will cause the loss of stereo channels when using other effects in front!
#define STEREO  false 

// Alarm interval in microseconds
#define DEBUG_INTERVAL_US   1000000  //  1.0  second
#define CPU_INTERVAL_US      500000  //  0.5  second
#define LED_INTERVAL_US       30000  //  30Hz  ~30ms
#define DISPLAY_INTERVAL_US   40000  //  25Hz  ~40ms
#define CONTROL_INTERVAL_US   10000  //  100Hz ~10ms

// Hold tap button for this long to save settings
#define HOLD_FOR_SAVE       5000000  //  5.0 seconds

// ============================================================================
// === Global Variables & Timers ==============================================
// ============================================================================

uint8_t selected_slot    = 0;       // Index of selected parameter (0–3)
bool toggle_lfo_led_flag = false;   // Toggle LED for LFO effect
bool lfo_update_led_flag = true;    // Flag to update LFO phase
bool updateDelayFlag     = false;   // Flag to signal delay parameter update

// CPU usage variables
float cpu0_peak_usage = 0.0f;        // Peak CPU usage in percentage
float cpu1_peak_usage = 0.0f;        // Peak CPU usage in percentage
uint64_t cpu0_peak_us = 0;           // Peak CPU usage in microseconds
uint64_t cpu1_peak_us = 0;           // Peak CPU usage in microseconds

// timers for various intervals
absolute_time_t last_pot_change_time;
static uint64_t last_print_time_us = 0;

// Global tap interval in milliseconds, initially 0 (no tempo)
uint32_t tap_interval_ms = 500;
bool tap_tempo_active_l = false;
bool tap_tempo_active_r = false;
bool activate_tap_flag = false;

// Delay time global for drawing the display
uint32_t delay_samples_l = 48000;
uint32_t delay_samples_r = 48000;

// Falsh saving flags
volatile bool save_request = false;         // Core1 -> Core0 request
volatile bool saving_in_progress = false;   // Core0 -> Core1 status
volatile bool ui_park_req = false;  // Core0 asks Core1 to park
volatile bool ui_park_ack = false;  // Core1 acknowledges it's parked

// Include the files where we dumped some of the code
#include "io.h"
#include "ui_main.h"
#include "var_conversion.h"
#include "audio.h"
#include "flash.h"

// Update on single-pot change for the selected preamp
static inline void update_preamp_from_pots(int changed_pot){
    if (changed_pot < 0 || changed_pot > 5) return;
    storedPreampPotValue[selected_preamp_style][changed_pot] = pot_value[changed_pot];
    switch (selected_preamp_style) {
        case FENDER:
            load_fender_params_from_memory();   break;
        case VOX_AC:
            load_vox_params_from_memory();      break;
        case MARSHALL:
            load_marshall_params_from_memory(); break;
        case SOLDANO:
            load_slo_params_from_memory(); break;
    }
}

// ============================================================================
// === POT changes and effects ================================================
// ============================================================================

typedef void (*EffectUpdateFn)(int changed_pot);

static EffectUpdateFn effect_param_updaters[NUM_EFFECTS] = {
    [CHRS_EFFECT_INDEX]     = update_chorus_params_from_pots,
    [COMP_EFFECT_INDEX]     = update_compressor_params_from_pots,
    [DELAY_EFFECT_INDEX]    = update_delay_params_from_pots,
    [DS_EFFECT_INDEX]       = update_distortion_params_from_pots,
    [EQ_EFFECT_INDEX]       = update_eq_params_from_pots,
    [FLNG_EFFECT_INDEX]     = update_flanger_params_from_pots,
    [FZ_EFFECT_INDEX]       = update_fuzz_params_from_pots,
    [OD_EFFECT_INDEX]       = update_overdrive_params_from_pots,
    [PHSR_EFFECT_INDEX]     = update_phaser_params_from_pots,
    [PREAMP_EFFECT_INDEX]   = update_preamp_from_pots,
    [REVB_EFFECT_INDEX]     = update_reverb_params_from_pots,
    [CAB_SIM_EFFECT_INDEX]  = update_speaker_sim_params_from_pots,
    [TREM_EFFECT_INDEX]     = update_tremolo_params_from_pots,
    [VIBR_EFFECT_INDEX]     = update_vibrato_params_from_pots
};

// ============================================================================
// === CPU RESOURCES ==========================================================
// ============================================================================

// === CPU0 usage ===
static volatile uint32_t interrupt_count = 0;
static volatile uint64_t total_duration_us = 0;
static uint64_t cpu0_loop_start_time_us = 0;

static inline void cpu0_task_start(void) {
    cpu0_loop_start_time_us = time_us_64();
}

static inline void cpu0_task_end(void) {
    uint64_t duration = time_us_64() - cpu0_loop_start_time_us;

    if (duration > cpu0_peak_us) {
        cpu0_peak_us = duration;
        cpu0_peak_usage = ((float)duration / sample_period_us) * 100.0f;
    }
}

void CPU_usage_counter() {
    static uint64_t last_reset_time = 0;
    uint64_t now = time_us_64();
    // Reset peak every 0.5 seconds (or any interval you want)
    if (now - last_reset_time >= CPU_INTERVAL_US) {
        last_reset_time = now;
        cpu0_peak_us = 0;
        cpu0_peak_usage = 0.0f;
    }
}

//=== CPU1 respource counter ===
uint64_t cpu1_loop_start_time_us = 0;
// CPU1 average tracking
static uint64_t cpu1_total_us = 0;
static uint32_t cpu1_sample_count = 0;
float cpu1_avg_usage = 0.0f;

static inline void cpu1_task_start(void) {
    cpu1_loop_start_time_us = time_us_64();
}

static inline void cpu1_task_end(void) {
    uint64_t duration = time_us_64() - cpu1_loop_start_time_us;

    // Peak tracking
    if (duration > cpu1_peak_us) {
        cpu1_peak_us = duration;
    }

    // Average tracking
    cpu1_total_us += duration;
    cpu1_sample_count++;
}


void update_cpu1_usage(float sample_period_us) {
    // Peak usage
    cpu1_peak_usage = ((float)cpu1_peak_us / sample_period_us) * 100.0f;

    // Average usage
    if (cpu1_sample_count > 0) {
        float avg_duration = (float)cpu1_total_us / cpu1_sample_count;
        cpu1_avg_usage = (avg_duration / sample_period_us) * 100.0f;
    } else {
        cpu1_avg_usage = 0.0f;
    }
}

void reset_cpu1_time(void){
    cpu1_peak_us = 0;
    cpu1_total_us = 0;
    cpu1_sample_count = 0;
}

// ============================================================================
// === AUDIO Processing =======================================================
// ============================================================================

// I2S configuration
static __attribute__((aligned(8))) pio_i2s i2s;

// Selected effects processing per sample
static inline __attribute__((always_inline))
void process_selected_effect_block(int slot, int32_t* in_l, int32_t* in_r, size_t frames) {
    switch (selectedEffects[slot]) {
        case CHRS_EFFECT_INDEX:
            chorus_process_block(in_l, in_r, frames, selected_chorus_mode); break;

        case COMP_EFFECT_INDEX:
            compressor_process_block(in_l, in_r, frames, STEREO); break;

        case DELAY_EFFECT_INDEX:
            delay_process_block(in_l, in_r, frames, selected_delay_mode); break;

        case DS_EFFECT_INDEX:
            distortion_process_block(in_l, in_r, frames, STEREO); break;

        case EQ_EFFECT_INDEX:
            eq_process_block(in_l, in_r, frames, STEREO); break;

        case FLNG_EFFECT_INDEX:
            flanger_process_block(in_l, in_r, frames, selected_flanger_mode); break;

        case FZ_EFFECT_INDEX:
            fuzz_process_block(in_l, in_r, frames, STEREO); break;

        case OD_EFFECT_INDEX:
            overdrive_process_block(in_l, in_r, frames, STEREO); break;

        case PHSR_EFFECT_INDEX:
            phaser_process_block(in_l, in_r, frames, selected_phaser_mode); break;

        case PREAMP_EFFECT_INDEX:   // [NEW]
            // Check what preamp processing is required
            switch (selected_preamp_style) {
                case FENDER:
                    fender_preamp_process_block(in_l, in_r, frames, STEREO);    break;
                case VOX_AC:
                    vox_preamp_process_block(in_l, in_r, frames, STEREO);       break;
                case MARSHALL:
                    marshall_preamp_process_block(in_l, in_r, frames, STEREO);  break;
                case SOLDANO:
                    slo_preamp_process_block(in_l, in_r, frames, STEREO);       break;
            } break;

        case REVB_EFFECT_INDEX:
            reverb_process_block(in_l, in_r, frames); break;

        case CAB_SIM_EFFECT_INDEX:
            speaker_sim_process_block(in_l, in_r, frames, STEREO); break;

        case TREM_EFFECT_INDEX:
            tremolo_process_block(in_l, in_r, frames, selected_tremolo_mode); break;

        case VIBR_EFFECT_INDEX:
            vibrato_process_block(in_l, in_r, frames, selected_vibrato_mode); break;
        default:
            break;
    }
}

// Split buffers across scratch banks (reduce bus contention)
static __attribute__((aligned(8), section(".scratch_x"))) int32_t buffer_l[AUDIO_BUFFER_FRAMES];
static __attribute__((aligned(8), section(".scratch_y"))) int32_t buffer_r[AUDIO_BUFFER_FRAMES];

// I2S audio processing
__attribute__((section(".time_critical"))) 
static void process_audio(const int32_t* input, int32_t* output, size_t num_frames) {
    
    // Start CPU counter
    if (SHOW_CPU) cpu0_task_start();

    local_peak_left  = 0;
    local_peak_right = 0;

    // De-interleave input
    for (size_t i = 0; i < num_frames; i++) {
        buffer_l[i] = input[i * 2 + 1];             
        if(!STEREO){ buffer_r[i] = buffer_l[i];  } // Input = Mono  
        else{        buffer_r[i] = input[i * 2]; } // Input = Stereo 
    }

    // Check the max inpu value to be shown in the VU meter
    if (currentUI == UI_VU_IN) {
        for (size_t i = 0; i < num_frames; i++) {
            process_audio_clipping(buffer_l[i], buffer_r[i], &local_peak_left, &local_peak_right);
        }
    }

    // RUn effects processing for each effects slot that is enabled
    for (int slot = 0; slot < 3; slot++) {
        if (led_state & (1 << slot)) {
            process_selected_effect_block(slot, buffer_l, buffer_r, num_frames);
        }
    }

    // Apply volume to each sample
    for (size_t i = 0; i < num_frames; i++) {
        process_audio_volume_sample(&buffer_l[i], &buffer_r[i]);
    }


    // Check the max output value to be shown in the VU meter
    if (currentUI == UI_VU_OUT) {
        for (size_t i = 0; i < num_frames; i++) {
            process_audio_clipping(buffer_l[i], buffer_r[i], &local_peak_left, &local_peak_right);
        }
    }
    
    // Check the gain reduction to be shown in the VU meter
    if (currentUI == UI_VU_GAIN) {
        // Just copy gain reduction dB values (already updated per sample)
    }

    // Interleave output
    for (size_t i = 0; i < num_frames; i++) {
        output[i * 2] = buffer_l[i];
        output[i * 2 + 1] = buffer_r[i];
    }

    // End CPU counter
    if (SHOW_CPU) cpu0_task_end();

    // Update peak values for VU meter
    peak_left = local_peak_left;
    peak_right = local_peak_right;
}


// I2S DMA interrupt handler
__attribute__((section(".time_critical"))) 
static void dma_i2s_in_handler(void) {
    /* We're double buffering using chained TCBs. By checking which buffer the
     * DMA is currently reading from, we can identify which buffer it has just
     * finished reading (the completion of which has triggered this interrupt).
     */
    if (*(int32_t**)dma_hw->ch[i2s.dma_ch_in_ctrl].read_addr == i2s.input_buffer) {
        // It is inputting to the second buffer so we can overwrite the first
        process_audio(i2s.input_buffer, i2s.output_buffer, AUDIO_BUFFER_FRAMES);
    } else {
        // It is currently inputting the first buffer, so we write to the second
        process_audio(&i2s.input_buffer[STEREO_BUFFER_SIZE], &i2s.output_buffer[STEREO_BUFFER_SIZE], AUDIO_BUFFER_FRAMES);
    }
    dma_hw->ints0 = 1u << i2s.dma_ch_in_data;  // clear the IRQ
}

// ============================================================================
// === FLASH - button check ===================================================
// ============================================================================

// Returns true while the TAP footswitch is physically held down.
// IMPLEMENT to match your board mapping.
// Example if 'footswitch_state' has TAP on bit 3 (LSB=slot1, slot2, slot3, TAP):
static inline bool tap_button_is_down(void) {
    // If you already expose a boolean for TAP, just return it here.
    const uint8_t TAP_MASK = (1u << 3);
    return (footswitch_state & TAP_MASK) != 0;
}

// ============================================================================
// === IO Actions =============================================================
// ============================================================================

#include "actions.h"

void handle_tap_tempo_button(){
    // --- TAP button: short press = tempo, long hold = save ---
    static bool tap_was_down     = false;
    static uint64_t tap_down_us  = 0;
    static bool saved_this_hold  = false;
    static uint64_t last_tap_us  = 0;   // for tempo

    bool tap = tap_button_is_down();
    uint64_t now_u = time_us_64();

    if (tap && !tap_was_down) {
        // Fresh press edge
        tap_down_us = now_u;
        saved_this_hold = false;
    }

    if (tap && tap_was_down) {
        // Still holding: check duration
        uint64_t held = now_u - tap_down_us;
        if (held >= HOLD_FOR_SAVE && !saved_this_hold) {
            save_request = true;     // trigger once
            saved_this_hold = true;
            if (DEBUG) printf("Long hold → save request!\n");
        }
    }

    if (!tap && tap_was_down) {
        // Released → only count as TAP TEMPO if NOT a long hold
        uint64_t held = now_u - tap_down_us;
        if (held < HOLD_FOR_SAVE && held > 50*1000) { // debounce 50 ms
            if (last_tap_us != 0) {
                uint32_t interval = (now_u - last_tap_us) / 1000; // ms
                if (interval >= 50 && interval <= 2000) {
                    tap_interval_ms = interval;
                    activate_tap_flag = true;
                    if (DEBUG) printf("Short tap → new tempo %u ms\n", tap_interval_ms);
                }
            }
            last_tap_us = now_u;
        }
    }

    tap_was_down = tap;
}

// ============================================================================
// === UI Generation ==========================================================
// ============================================================================

#include "ui_draw.h"

// ============================================================================
// === Debug Information ======================================================
// ============================================================================

// === RAM allocation ===
extern char __StackLimit;   // End of stack
extern char __bss_end__;    // End of static/global data

size_t get_free_ram_bytes(void) {
    return (size_t)(&__StackLimit - &__bss_end__);
}

float get_free_ram_percent(void) {
    return ((float)get_free_ram_bytes() / (264 * 1024.0f)) * 100.0f;
}

// === FLASH allocation ===
extern uint8_t __flash_binary_start;
extern uint8_t __flash_binary_end;

size_t get_flash_used_bytes(void) {
    return (size_t)(&__flash_binary_end - &__flash_binary_start);
}

float get_flash_used_percent(void) {
    const size_t total_flash = 2 * 1024 * 1024;  // 2MB
    return ((float)get_flash_used_bytes() / total_flash) * 100.0f;
}

// === EFFECTS status ===
void print_enabled_effects(void) {
    printf("Enabled effects:");

    for (int slot = 0; slot < 3; slot++) {
        if (led_state & (1 << slot)) {
            int effect_index = selectedEffects[slot];
            if (effect_index >= 0 && effect_index < NUM_EFFECTS) {
                printf("\n - Slot %d: %s ", slot + 1, allEffects[effect_index]);

                // Print the effects mode as well 
                switch (effect_index) {
                    case CHRS_EFFECT_INDEX:
                        printf("- %s", chorus_mode_names[selected_chorus_mode]);   break;
                    case DELAY_EFFECT_INDEX:
                        printf("- %s", delay_mode_names[selected_delay_mode]);     break;
                    case FLNG_EFFECT_INDEX:
                        printf("- %s", stereo_mode_names[selected_flanger_mode]);  break;
                    case PHSR_EFFECT_INDEX:
                        printf("- %s", stereo_mode_names[selected_phaser_mode]);   break;                
                    case PREAMP_EFFECT_INDEX:
                        printf("- %s", preamp_names[selected_preamp_style]);       break;
                    case TREM_EFFECT_INDEX:
                        printf("- %s", stereo_mode_names[selected_tremolo_mode]);  break;
                    case VIBR_EFFECT_INDEX:
                        printf("- %s", stereo_mode_names[selected_vibrato_mode]);  break;
                }
            } else {
                printf("\n - Slot %d: (Invalid effect index: %d)", slot + 1, effect_index);
            }
        }
    }
    printf("\n"); // New Line
}

// ============================================================================
// === Clocks =================================================================
// ============================================================================

// === Clock Setup ===
void setup_system_and_peripheral_clocks() {
    // Set system clock to 250 MHz using default PLLs
    set_sys_clock_khz(SYSTEM_CLOCK_MHZ * 1000, true);

    // this sets up USB and might reconfigure clk_peri
    stdio_init_all();  

    // Important: USB takes time to enumerate and reset clocks
    sleep_ms(100);  // ← Give USB time to settle and override clocks

    // Read settings stored in flash
    init_settings_from_flash();

    // Set clk_peri to 125 MHz (from clk_sys)
    clock_configure(
        clk_peri,
        0, // No glitchless mux
        CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS,
        SYSTEM_CLOCK_MHZ * 1000 * 1000,    // Source frequency (clk_sys)
        PERIPHERAL_CLOCK_MHZ * 1000 * 1000 // Desired frequency
    );
}

// === Get I2C Frequency ===
uint32_t i2c_get_freq(i2c_inst_t *i2c) {
    i2c_hw_t *hw = (i2c == i2c0) ? i2c0_hw : i2c1_hw;
    uint32_t hcnt = hw->fs_scl_hcnt;
    uint32_t lcnt = hw->fs_scl_lcnt;
    return clock_get_hz(clk_peri) / (hcnt + lcnt + 2);
}

// === Print Current Clock Speeds ===
void print_clock_info() {
    printf("Clock Frequencies:\n");
    printf(" - clk_sys     = %0.2f MHz\n", (double)clock_get_hz(clk_sys)  / 1e6);
    printf(" - clk_peri    = %0.2f MHz\n", (double)clock_get_hz(clk_peri) / 1e6);
    printf(" - clk_usb     = %0.2f MHz\n", (double)clock_get_hz(clk_usb)  / 1e6);
    printf(" - clk_adc     = %0.2f MHz\n", (double)clock_get_hz(clk_adc)  / 1e6);
    printf(" - clk_rtc     = %0.2f kHz\n", (double)clock_get_hz(clk_rtc)  / 1e3);
    printf(" - SPI1 actual = %0.2f MHz\n", (double)spi_get_baudrate(spi1) / 5e5); // Baud * x2
    printf(" - I2C0 actual = %0.2f kHz\n", (double)i2c_get_freq(i2c0)     / 5e2); // Baud * x2   
}

// ============================================================================
// === SECOND Control CORE ====================================================
// ============================================================================

static __not_in_flash_func(void core1_park_loop)(void) {
    uint32_t irq = save_and_disable_interrupts();  // stop ISRs that might live in flash
    ui_park_ack = true;
    while (ui_park_req) {
        __wfe();   // low-power wait, executes from ROM
    }
    ui_park_ack = false;
    restore_interrupts(irq);
}

volatile bool dsp_ready = false;

void second_thread() {
    I2C_Initialize(I2C_TARGET_HZ);
    SSD1306_Init();
    SSD1306_ClearScreen();
    
    // Draw centered logo
    SSD1306_DrawSplashLogoBitmap(32, 0, true);

    // Setup encoder, GPIO expander, and potentiometers
    setup_encoder();
    spi_ram_init(SPI_TARGET_HZ / 2);
    setup_pca9555_interrupt();   // set before global IRQ handler
    setup_global_irq_handler();  // must be after the above
    initialize_potentiometers();
    initialize_gpio_expander();
    
    // Call audio init functions
    reverb_init();
    init_chorus();
    init_phaser();
    init_delay();
    init_compressor();
    init_speaker_sim();

    last_pot_change_time = get_absolute_time();
    sleep_ms(10);
    read_all_pots(true);

    // Update the parameters based on stored pot values
    load_chorus_parms_from_memory();
    load_compressor_parms_from_memory();
    load_delay_parms_from_memory();
    load_distortion_parms_from_memory();
    load_eq_parms_from_memory();
    load_flanger_parms_from_memory();
    load_fuzz_parms_from_memory();
    load_overdrive_parms_from_memory();
    load_phaser_parms_from_memory();
    load_reverb_parms_from_memory();
    load_speaker_sim_parms_from_memory();
    load_tremolo_parms_from_memory();
    load_vibrato_parms_from_memory();
    
    load_fender_params_from_memory();
    load_vox_params_from_memory();
    load_marshall_params_from_memory();
    load_slo_params_from_memory();   

    // Update the volume based on the curret potentiometer state
    update_volume_from_pot();

    int changed = -1;
    dsp_ready = true;   // <<< signal ready

    // Optionally wait some time to show the logo
    sleep_ms(1000);
    SSD1306_UpdateScreen();
    SetFont(&Font8x8);

    // Timer tracking variables
    uint64_t last_debug_time   = time_us_64();
    uint64_t last_lfo_time     = time_us_64();
    uint64_t last_display_time = time_us_64();
    uint64_t last_control_time = time_us_64();

    while (true) {

        // --- Cooperative park: when Core0 wants to write flash, we stop touching I2C/OLED/etc.
        if (ui_park_req) { core1_park_loop(); continue; }

        // CPU resource counter 
        cpu1_task_start();

        uint64_t now = time_us_64();

        // Shared GPIO interrupt handling
        if (pca9555_interrupt_flag) {
            pca9555_interrupt_flag = false;
            
            // Read the GPIO expander state until we get the same state three times in a row
            uint8_t input_port0_prev[3] = {0x01, 0x02, 0x03};
            uint8_t input_port1_prev[3] = {0x01, 0x02, 0x03};

            while (true) {
                // Shift previous readings
                input_port0_prev[0] = input_port0_prev[1];
                input_port0_prev[1] = input_port0_prev[2];

                input_port1_prev[0] = input_port1_prev[1];
                input_port1_prev[1] = input_port1_prev[2];

                // Read new state
                update_gpio_expander_state();
                input_port0_prev[2] = input_port0;
                input_port1_prev[2] = input_port1;

                // Check if last 3 readings are identical
                if (input_port0_prev[0] == input_port0_prev[1] &&
                    input_port0_prev[1] == input_port0_prev[2] &&
                    input_port1_prev[0] == input_port1_prev[1] &&
                    input_port1_prev[1] == input_port1_prev[2]) {
                    break;
                }

                sleep_us(DEBOUNCE_US);
            }

            // Handle footswitches
            uint8_t switch_pressed = handle_footswitches();
            if (switch_pressed > 0) {
                selected_slot = switch_pressed - 1; // Convert to 0-based index
            }

            for (int slot = 0; slot < 3; slot++) {
                // Was previously ON, now OFF
                if ((prev_led_state & (1 << slot)) && !(led_state & (1 << slot))) {
                    if (selectedEffects[slot] == DELAY_EFFECT_INDEX) {
                        clear_delay_memory();  // Call the function we made before
                        if(DEBUG) printf("Delay memory cleared for slot %d\n", slot + 1);
                    }
                    else if (selectedEffects[slot] == REVB_EFFECT_INDEX) {
                        clear_reverb_memory();  // Call the function we made before
                        if(DEBUG) printf("Reverb memory cleared for slot %d\n", slot + 1);
                    }
                }
            }

            prev_led_state = led_state;

            // Handle encoder button if pressed
            if (encoder_button == 1) {
                handleButtonPress();
            }
            
            if(DEBUG && PRINT_IO){
                // Optional: debug log current GPIO state
                printf("PCA9555 state: FootSW: %02X, Dipswitch: %02X, Encoder Button: %d\n",
                    footswitch_state, dipswitch_state, encoder_button);
                // debug log with LED state
                printf("LED state: %02X\n", led_state);
            }        
        }

        // Handle tap tempo button
        handle_tap_tempo_button();

        // Update tap tempo on flag
        if (activate_tap_flag){
            for (int i = 0; i < 3; i++){
                tap_tempo_active_l = true;
                tap_tempo_active_r = true;
                if(selectedEffects[i] == DELAY_EFFECT_INDEX){
                    load_delay_parms_from_memory(); // Reload delay params to apply new tempo
                }                
            }   
            activate_tap_flag = false;         
        }

        // Update the delay settings if the tempo has changed
        if(updateDelayFlag){
            //if(DEBUG) { printf("Updating L|R delay: %s | %s\n", delay_fraction_name[delay_time_fraction_l], delay_fraction_name[delay_time_fraction_r]);}
            load_delay_parms_from_memory(); // Reload delay params to apply new tempo
            updateDelayFlag = false;
        }
        
        // BLink the tap tempo and LFO LED in sync with delay time if delay is selected
        if(selectedEffects[selected_slot] == DELAY_EFFECT_INDEX){
            // Calculate intervals in ms from delay_samples and SAMPLE_RATE
            uint32_t interval_l = (uint32_t)((float)delay_samples_l * 1000.0f / (float)SAMPLE_RATE);
            uint32_t interval_r = (uint32_t)((float)delay_samples_r * 1000.0f / (float)SAMPLE_RATE);

            // Clamp to avoid too-fast blinking
            if (interval_l < 50) interval_l = 50;
            if (interval_r < 50) interval_r = 50;

            static absolute_time_t next_blink_time_l = {0};
            static absolute_time_t next_blink_time_r = {0};
            static bool blink_state_l = false;
            static bool blink_state_r = false;

            absolute_time_t now = get_absolute_time();

            // Tap LED (left delay)
            if (absolute_time_diff_us(now, next_blink_time_l) <= 0) {
                blink_state_l = !blink_state_l;
                if (blink_state_l)
                    led_state |= (1 << 3);  // LED 3 ON (Tap LED)
                else
                    led_state &= ~(1 << 3); // LED 3 OFF

                next_blink_time_l = delayed_by_ms(now, interval_l / 2);
            }

            // LFO LED (right delay)
            if (absolute_time_diff_us(now, next_blink_time_r) <= 0) {
                blink_state_r = !blink_state_r;
                lfo_led_state = blink_state_r;
                next_blink_time_r = delayed_by_ms(now, interval_r / 2);
            }
        }
        // Otherwise, just use tap tempo blink
        else{
            update_tap_blink();
        }
 
        // Update control parameters and read pots at regular intervals
        if (now - last_control_time >= CONTROL_INTERVAL_US) {
            last_control_time += CONTROL_INTERVAL_US;
            // Read potentiometers and update values
            changed = read_all_pots(false);
            // Update delay time based on potentiometer value
            if (changed >= 0) {
                // Update settings for all pots

                // Print the selecyed effect
                // if(DEBUG) printf("Selected effect: %s\n", allEffects[selectedEffects[selected_slot]]);

                int effect_index = selectedEffects[selected_slot];
                if (effect_index >= 0 && effect_index < NUM_EFFECTS && effect_param_updaters[effect_index]) {
                    effect_param_updaters[effect_index](changed);
                }

                update_volume_from_pot();
                // Reset the last pot change time
                last_pot_change_time = get_absolute_time();
            }
        }
        // Only tick LEDs when we're NOT saving or being asked to park
        if (!saving_in_progress && !ui_park_req) {
            // check if it is time to update the LEDs
            if (now - last_lfo_time >= LED_INTERVAL_US) {
                last_lfo_time += LED_INTERVAL_US;

                // Let the audio core know that it can update the LFO LED
                lfo_update_led_flag = true;

                // Bits 0–3: led_state, Bits 4–6: zero, Bit 7: lfo_led_state
                uint8_t port1_value = (lfo_led_state << 7) | (led_state & 0x0F);

                // Write to OUTPUT_PORT1 (address 0x03)
                uint8_t out[] = { PCA9555_OUTPUT_PORT1, port1_value };
                i2c_write_blocking(I2C_PORT, PCA9555_ADDR, out, 2, false);
            }
        }
        
        // If we’re saving, show a blocking overlay and skip normal UI updates
        static bool saving_drawn = false;
        if (saving_in_progress) {
            if (!saving_drawn) {
                SetFont(&Font8x8);
                SSD1306_ClearScreen();
                // Centered "SAVING..."
                const char* msg = "SAVING...";
                // 128x64 display, 8x8 font -> estimate centering
                SSD1306_DrawString( (128 - (int)strlen(msg)*8)/2, (64-8)/2, msg, false);
                SSD1306_UpdateScreen();
                saving_drawn = true;
            }
            // While saving, keep LEDs and I/O alive, but skip drawUI()
        } else {
            saving_drawn = false;
            // Normal UI cadence
            if (now - last_display_time >= DISPLAY_INTERVAL_US) {
                last_display_time += DISPLAY_INTERVAL_US;
                drawUI(changed);
            }
        }       

        // Update the CPU usage 
        if(SHOW_CPU) CPU_usage_counter();

        // Print debug info to terminal
        if (now - last_debug_time >= DEBUG_INTERVAL_US) {
            last_debug_time += DEBUG_INTERVAL_US;
            if(DEBUG){
                
                if(PRINT_I2S){
                    printf("_________________________\n");
                    printf("%d samples @ %d kHz | %.1f us\n", AUDIO_BUFFER_FRAMES, SAMPLE_RATE / 1000, sample_period_us);
                }
                if(PRINT_CPU){ 
                    printf("-------------------------\n");      
                    // Print CPU resource usage compared to sample block period
                    // More than 100% means the CPU was busy for more than one sample period (at least once) 
                    printf("CPU0  : %.1f%%\n", cpu0_peak_usage); 
                    update_cpu1_usage(sample_period_us); // get percentage
                    printf("CPU1  : %.1f%% | ~%.1f%%\n", cpu1_peak_usage, cpu1_avg_usage);
                    reset_cpu1_time();  // reset counters
                }
                if(PRINT_RAM){   
                    printf("RAM   : %.1f%% | %d bytes\n", get_free_ram_percent(), get_free_ram_bytes());
                }
                if(PRINT_FLASH){    
                    printf("FLASH : %.1f%% | %d bytes\n", get_flash_used_percent(), get_flash_used_bytes());
                }          
                if(PRINT_CLOCK){     
                    printf("-------------------------\n");  
                    print_clock_info();
                }      
                if(PRINT_EFFECTS){
                    printf("-------------------------\n");
                    print_enabled_effects();
                }   
            }
        }
        
        // Hint for power savings
        tight_loop_contents();  

        // End the CPU1 task and update the peak usage
        cpu1_task_end();
    }
}

// ============================================================================
// === MAIN Audio CORE ========================================================
// ============================================================================

int main() {
    // Overclock the system 
    setup_system_and_peripheral_clocks();

    // Launch OLED/UI on second core
    multicore_launch_core1(second_thread);
    // Wait for Core 1 to be ready
    while (!dsp_ready) tight_loop_contents();

    // Setup audio
    i2s_program_start_synched(pio0, &i2s_config_default, dma_i2s_in_handler, &i2s);

    // Calculate sample time
    sample_period_us = (1000000.0f * AUDIO_BUFFER_FRAMES) / SAMPLE_RATE;

    while (true) {
        sleep_ms(1);

        // Perform requested save on Core 0
        if (save_request) {
            if(DEBUG) printf("Start saving to flash:\n");
            saving_in_progress = true;
            __sev();
            sleep_ms(5);              // let OLED draw "SAVING..."

            // Wait for core 1 to park
            ui_park_req = true;
            __sev();
            while (!ui_park_ack) { 
                tight_loop_contents();
                sleep_ms(250);
                // if(DEBUG) printf("Waiting for Core1 to park...\n");
            }

            save_all_settings_to_flash();   // <-- no args

            // Wait for core 1 to un-park
            ui_park_req = false;
            __sev();
            while (ui_park_ack) { 
                tight_loop_contents(); 
                sleep_ms(250);
                // if(DEBUG) printf("Waiting for Core1 to un-park...\n");
            }

            saving_in_progress = false;
            save_request = false;

            if(DEBUG) printf("Settings saved to flash!\n");
        }
    }
}