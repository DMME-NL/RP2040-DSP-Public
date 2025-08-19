/* 
 * Author: Milan Wendt
 * Date:   2025-08-19
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
#define DEBUG               0  // Enable debug mode for additional logging
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
#define DEFAULT_LED_STATE   0x00  // Default LED / EFFECT state 

// Alarm interval in microseconds
#define DEBUG_INTERVAL_US   1000000  // 1.0 second
#define CPU_INTERVAL_US      500000  // 0.5 second
#define LED_INTERVAL_US       30000  // 30Hz  ~30ms
#define DISPLAY_INTERVAL_US   33333  // 30Hz  ~33ms
#define CONTROL_INTERVAL_US   10000  // 100Hz ~10ms

// ============================================================================
// === Global Variables & Timers ==============================================
// ============================================================================

uint8_t selected_slot = 0;          // Index of selected parameter (0–3)
bool toggle_lfo_led_flag = false;   // Toggle LED for LFO effect
bool lfo_update_led_flag = true;    // Flag to update LFO phase

// CPU usage variables
float cpu0_peak_usage = 0.0f;        // Peak CPU usage in percentage
float cpu1_peak_usage = 0.0f;        // Peak CPU usage in percentage
uint64_t cpu0_peak_us = 0;           // Peak CPU usage in microseconds
uint64_t cpu1_peak_us = 0;           // Peak CPU usage in microseconds

// timers for various intervals
absolute_time_t last_pot_change_time;
static uint64_t last_print_time_us = 0;

// Include the files where we dumped some of the code
#include "io.h"
#include "ui.h"
#include "var_conversion.h"
#include "audio.h"

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
    [FZ_EFFECT_INDEX]        = update_fuzz_params_from_pots,
    [OD_EFFECT_INDEX]       = update_overdrive_params_from_pots,
    [PHSR_EFFECT_INDEX]     = update_phaser_params_from_pots,
    [PREAMP_EFFECT_INDEX]   = NULL,
    [REVB_EFFECT_INDEX]     = update_reverb_params_from_pots,
    [CAB_SIM_EFFECT_INDEX]  = update_speaker_sim_params_from_pots,
    [TREM_EFFECT_INDEX]     = update_tremolo_params_from_pots,
    [VIBR_EFFECT_INDEX]     = update_vibrato_params_from_pots

    /*
        TODO - ADD NEW EFFECTS HERE
    */
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
            compressor_process_block(in_l, in_r, frames); break;

        case DELAY_EFFECT_INDEX:
            delay_process_block(in_l, in_r, frames, selected_delay_mode); break;

        case DS_EFFECT_INDEX:
            distortion_process_block(in_l, in_r, frames); break;

        case EQ_EFFECT_INDEX:
            eq_process_block(in_l, in_r, frames); break;

        case FLNG_EFFECT_INDEX:
            flanger_process_block(in_l, in_r, frames, selected_flanger_mode); break;

        case FZ_EFFECT_INDEX:
            fuzz_process_block(in_l, in_r, frames); break;

        case OD_EFFECT_INDEX:
            overdrive_process_block(in_l, in_r, frames); break;

        case PHSR_EFFECT_INDEX:
            phaser_process_block(in_l, in_r, frames, selected_phaser_mode); break;

        case PREAMP_EFFECT_INDEX:
            /* Not implemented */ break;

        case REVB_EFFECT_INDEX:
            reverb_process_block(in_l, in_r, frames); break;

        case CAB_SIM_EFFECT_INDEX:
            speaker_sim_process_block(in_l, in_r, frames); break;

        case TREM_EFFECT_INDEX:
            tremolo_process_block(in_l, in_r, frames, selected_tremolo_mode); break;

        case VIBR_EFFECT_INDEX:
            vibrato_process_block(in_l, in_r, frames, selected_vibrato_mode); break;

        /*
            TODO - ADD NEW EFFECTS HERE
        */

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

    // Removed and defined above
    // static int32_t buffer_l[AUDIO_BUFFER_FRAMES];
    // static int32_t buffer_r[AUDIO_BUFFER_FRAMES];

    local_peak_left = 0;
    local_peak_right = 0;

    // De-interleave input
    for (size_t i = 0; i < num_frames; i++) {
        buffer_r[i] = input[i * 2];
        buffer_l[i] = input[i * 2 + 1];
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
// === IO Actions =============================================================
// ============================================================================

void handleButtonPress() {

    if(DEBUG && PRINT_ACTION){ printf("Button pressed, current UI: %d, encoder position: %d\n", currentUI, encoder_position);}

    if (currentUI == UI_HOME) {
        if (encoder_position == 1) {
            // Enter effect list UI
            currentUI = UI_EFFECT_LIST;
            encoder_position = selectedEffects[selected_slot];  // Preselect the current effect name
        } else if (encoder_position >= 2 && encoder_position <= 4) {
            // Select effect number (1–3)
            int new_selected_slot = encoder_position - 2;
            if (selected_slot != new_selected_slot) {
                selected_slot = new_selected_slot;
                param_selected = true;
            }
        } else if (encoder_position == 5) {
            // Left or right arrow triggers UI switch
            currentUI = UI_VU_IN;
            encoder_position = 1;  // Set pointer to right arrow
        } else if (encoder_position == 0) {
            // Left or right arrow triggers UI switch
            currentUI = UI_VU_OUT;
        }
    }
     
    else if (currentUI == UI_VU_IN) {
        // If encoder position is 0, go back to home
        if (encoder_position == 0) { 
            currentUI = UI_HOME;  
        } 
        else if (encoder_position == 1) { 
            // If compressor is selected, show gain reduction
            if(selectedEffects[selected_slot] == COMP_EFFECT_INDEX) {
                currentUI = UI_VU_GAIN; 
            }
            // Otherwise, go to output VU
            else{ currentUI = UI_VU_OUT; }
        }
    } 

    else if (currentUI == UI_VU_OUT) {
        if (encoder_position == 0) { 
            // If compressor is selected, show gain reduction
            if(selectedEffects[selected_slot] == COMP_EFFECT_INDEX) {
                currentUI = UI_VU_GAIN; 
            }
            // Otherwise, go back to input VU
            else{ currentUI = UI_VU_IN; }
        } 
        // Otherwise, go back to home
        else if (encoder_position == 1) { 
            currentUI = UI_HOME;
            encoder_position = 5;  // Set pointer to right arrow
        }
    }

    else if (currentUI == UI_VU_GAIN) {
        if (encoder_position == 0) { currentUI = UI_VU_IN;  } 
        else{                        currentUI = UI_VU_OUT; }
    }

    else if (currentUI == UI_EFFECT_LIST) {
        // Apply selected effect to the correct slot and return to home
        // Check if the effect is already selected in another slot
        bool effectAlreadySelected = false;
        for (int i = 0; i < 3; ++i) {
            if (i != selected_slot && selectedEffects[i] == effectListIndex) {
                // Effect already selected in another slot, show error
                // Print error message to serial console
                if(DEBUG) printf("Effect already selected in slot %d\n", i + 1);
                effectAlreadySelected = true;
                return;
            }
        }
        if (!effectAlreadySelected) {
            selectedEffects[selected_slot] = effectListIndex;
            param_selected = true;

            // Show menu for delay mode if delay effect is selected
            if (effectListIndex == DELAY_EFFECT_INDEX) {
                delay_mode_menu_index = selected_delay_mode;
                encoder_position = delay_mode_menu_index;
                currentUI = UI_DELAY_MODE_MENU;
            } 
            // Show menu for chorus effect
            else if (effectListIndex == CHRS_EFFECT_INDEX){
                chorus_mode_menu_index = selected_chorus_mode;
                encoder_position = chorus_mode_menu_index;
                currentUI = UI_CHORUS_MODE_MENU;
            } 
            // Show menu for stereo / mono mode for flanger effects
            else if (effectListIndex == FLNG_EFFECT_INDEX){
                stereo_mode_menu_index = selected_flanger_mode;
                encoder_position = stereo_mode_menu_index;
                currentUI = UI_STEREO_MODE_MENU;
            } 
            // Show menu for stereo / mono mode for phaser effects
            else if (effectListIndex == PHSR_EFFECT_INDEX){
                stereo_mode_menu_index = selected_phaser_mode;
                encoder_position = stereo_mode_menu_index;
                currentUI = UI_STEREO_MODE_MENU;
            } 
            else if (effectListIndex == TREM_EFFECT_INDEX){
                stereo_mode_menu_index = selected_tremolo_mode;
                encoder_position = stereo_mode_menu_index;
                currentUI = UI_STEREO_MODE_MENU;
            } 
            else if (effectListIndex == VIBR_EFFECT_INDEX){
                stereo_mode_menu_index = selected_vibrato_mode;
                encoder_position = stereo_mode_menu_index;
                currentUI = UI_STEREO_MODE_MENU;
            } 
            // Any other effect just returns to home
            else {
                encoder_position = 1;
                currentUI = UI_HOME;
            }
        }
    }
    // Set the selected delay / chorus / stereo mode and return to home
    else if (currentUI == UI_DELAY_MODE_MENU || UI_CHORUS_MODE_MENU || UI_STEREO_MODE_MENU) {
        // Changin the mode hapens in the draw function
        // That way we can update the selected mode in real time
        encoder_position = 1;  // reset to effect name
        currentUI = UI_HOME;
    } 
    // Undefined UI and action
    else {
        if(DEBUG){ 
            printf("[!] Undefined UI for button press: %d\n", currentUI); 
            printf("    Returning back to HOME\n"); 
        }
        encoder_position = 1;  // reset to effect name
        currentUI = UI_HOME;
    } 
}

// ============================================================================
// === UI Generation ==========================================================
// ============================================================================

void drawUI(int changed_pot_index) {
    SSD1306_ClearScreen();

    // Handle pot interaction and timeout
    if (changed_pot_index >= 0) {
        if (currentUI != UI_POT) {
            previousUI = currentUI;
        }
        currentUI = UI_POT;
        last_changed_pot = changed_pot_index;
        last_pot_change_time = get_absolute_time();
    }

    if (currentUI == UI_POT) {
        int64_t us_since_change = absolute_time_diff_us(last_pot_change_time, get_absolute_time());
        if (us_since_change > 500000) {
            currentUI = previousUI;
        }
    }

    // UI rendering logic
    switch (currentUI) {
        case UI_HOME:
            // Wrap encoder position around home screen
            if (encoder_position < 0) encoder_position = 5;
            if (encoder_position > 5) encoder_position = 0;
            drawHomeScreen(encoder_position, param_selected, selected_slot);
            break;

        case UI_EFFECT_LIST:
            // Wrap encoder around effect list entries
            if (encoder_position < 0) encoder_position = NUM_EFFECTS - 1;
            if (encoder_position >= NUM_EFFECTS) encoder_position = 0;
            effectListIndex = encoder_position;
            drawEffectListScreen(effectListIndex);
            break;

        case UI_DELAY_MODE_MENU:
            // Wrap encoder
            if (encoder_position < 0) encoder_position = NUM_DELAY_MODES - 1;
            if (encoder_position >= NUM_DELAY_MODES) encoder_position = 0;

            delay_mode_menu_index = encoder_position;
            drawDelayModeMenu(delay_mode_menu_index);
            break;

        case UI_CHORUS_MODE_MENU:
            // Wrap encoder
            if (encoder_position < 0) encoder_position = NUM_CHORUS_MODES - 1;
            if (encoder_position >= NUM_CHORUS_MODES) encoder_position = 0;

            chorus_mode_menu_index = encoder_position;
            drawChorusModeMenu(chorus_mode_menu_index);
            break;

        case UI_STEREO_MODE_MENU:
            // Wrap encoder
            if (encoder_position < 0) encoder_position = NUM_STEREO_MODES - 1;
            if (encoder_position >= NUM_STEREO_MODES) encoder_position = 0;

            stereo_mode_menu_index = encoder_position;
            drawStereoModeMenu(stereo_mode_menu_index);
            break;

        case UI_POT:
            drawPotScreen(last_changed_pot, encoder_position);
            break;

        case UI_VU_IN:
            // Wrap encoder position on VU screen
            if (encoder_position < 0) encoder_position = 1;
            if (encoder_position > 1) encoder_position = 0;

            if (absolute_time_diff_us(last_sample_time, get_absolute_time()) > 25000) {
                last_sample_time = get_absolute_time();
                peak_left_block = peak_left;
                peak_right_block = peak_right;
                peak_left = 0;
                peak_right = 0;
            }

            drawVUMeterScreen(peak_left_block, peak_right_block, encoder_position, VU_INPUT);
            break;

        case UI_VU_OUT:
            // Wrap encoder position on VU screen
            if (encoder_position < 0) encoder_position = 1;
            if (encoder_position > 1) encoder_position = 0;

            if (absolute_time_diff_us(last_sample_time, get_absolute_time()) > 25000) {
                last_sample_time = get_absolute_time();
                peak_left_block = peak_left;
                peak_right_block = peak_right;
                peak_left = 0;
                peak_right = 0;
            }

            drawVUMeterScreen(peak_left_block, peak_right_block, encoder_position, VU_OUTPUT);
            break;

        case UI_VU_GAIN:
            // Wrap encoder position on VU screen
            if (encoder_position < 0) encoder_position = 1;
            if (encoder_position > 1) encoder_position = 0;

            if (absolute_time_diff_us(last_sample_time, get_absolute_time()) > 25000) {
                last_sample_time = get_absolute_time();

                // Convert linear gain to float (0.0 to 1.0)
                float gain_l = q24_to_float(comp_linear_gain_q24_l);
                float gain_r = q24_to_float(comp_linear_gain_q24_r);

                // Convert to dB scale
                float gain_db_l = 20.0f * log10f(gain_l);
                float gain_db_r = 20.0f * log10f(gain_r);

                // Clamp to expected dB range (e.g. -40 dB to 0 dB)
                if (gain_db_l < -40.0f) gain_db_l = -40.0f;
                if (gain_db_r < -40.0f) gain_db_r = -40.0f;
                if (gain_db_l > 0.0f) gain_db_l = 0.0f;
                if (gain_db_r > 0.0f) gain_db_r = 0.0f;

                // Map -40 dB → 0, 0 dB → max needle value
                const float VU_SCALE = 2147483392.0f;  // Match drawVUMeter input range
                peak_left_block  = (uint32_t)((gain_db_l + 40.0f) * (VU_SCALE / 40.0f));
                peak_right_block = (uint32_t)((gain_db_r + 40.0f) * (VU_SCALE / 40.0f));
            }

            drawVUMeterScreen(peak_left_block, peak_right_block, encoder_position, VU_GAIN);
            break;

    }

    SSD1306_UpdateScreen();
}

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
    printf("Enabled effects:\n");

    for (int slot = 0; slot < 3; slot++) {
        if (led_state & (1 << slot)) {
            int effect_index = selectedEffects[slot];
            if (effect_index >= 0 && effect_index < NUM_EFFECTS) {
                printf(" - Slot %d: %s\n", slot + 1, allEffects[effect_index]);
            } else {
                printf(" - Slot %d: (Invalid effect index: %d)\n", slot + 1, effect_index);
            }
        }
    }
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

void second_thread() {
    I2C_Initialize(I2C_TARGET_HZ);
    SSD1306_Init();
    SSD1306_ClearScreen();
    
    // Draw centered logo
    SSD1306_DrawSplashLogoBitmap(32, 0, true);
    
    // Optionally wait some time
    sleep_ms(1000);
    SSD1306_UpdateScreen();
    SetFont(&Font8x8);

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
    // load_preamp_parms_from_memory();
    load_reverb_parms_from_memory();
    load_speaker_sim_parms_from_memory();
    load_tremolo_parms_from_memory();
    load_vibrato_parms_from_memory();
    
    /*
        TODO - ADD NEW EFFECTS HERE
    */

    // Update the volume based on the curret potentiometer state
    update_volume_from_pot();

    int changed = -1;

    // Timer tracking variables
    uint64_t last_debug_time   = time_us_64();
    uint64_t last_lfo_time     = time_us_64();
    uint64_t last_display_time = time_us_64();
    uint64_t last_control_time = time_us_64();

    while (true) {
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
        
        // Updates tap tempo LED blinking
        update_tap_blink();

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

        // update the UI display every DISPLAY_INTERVAL_US
        if (now - last_display_time >= DISPLAY_INTERVAL_US) {
            last_display_time += DISPLAY_INTERVAL_US;
            drawUI(changed);
        }

        // Update the CPU usage 
        if(SHOW_CPU){   CPU_usage_counter(); }
        
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

    // Setup audio
    i2s_program_start_synched(pio0, &i2s_config_default, dma_i2s_in_handler, &i2s);

    // Calculate sample time
    sample_period_us = (1000000.0f * AUDIO_BUFFER_FRAMES) / SAMPLE_RATE;

    // Launch OLED/UI on second core
    multicore_launch_core1(second_thread);

    while (true) {
        // Audio runs in interrupts, no need to block here
        sleep_ms(1);      
    }
}