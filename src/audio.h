/* audio.h
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

// ============================================================================
// === Constants and Macros ===================================================
// ============================================================================

#define PEAK_MAX        0x7FFFFF00      // Largest 24-bit sample for peak detection (~24-bit max)
#define PEAK_MIN       -0x7FFFFF00      // Largest 24-bit sample for peak detection (~24-bit max)

// --- Filter coefficients (Q8.24) ---
// Tuned for guitar, approximate center frequencies
#define BASS_A_Q24      (0x0003FD65) // 120 Hz
#define MID_A_Q24       (0x0013563F) // 600 Hz
#define TREBLE_A_Q24    (0x00579B7C) // 3.2 kHz

// Global HPF and LPF
#define HPF_A_Q24       (0x0002FF8C) // 90  Hz
#define LPF_A_Q24       (0x0092ACAE) // 6.5 kHz

// Additional EQ frequencies
#define LOW_A_Q24       (0x00355EC) // 100 Hz
#define LOW_MID_A_Q24   (0x009DE1C) // 300 Hz
#define HIGH_MID_A_Q24  (0x01E0870) // 1.0 kHz
#define HIGH_A_Q24      (0x0385A9C) // 2.0 kHz

// --- Gain constants (Q8.24) ---
#define MIN_GAIN_Q24    0x00000000  // 0.0

// ============================================================================
// === Global Variables =======================================================
// ============================================================================

volatile int32_t peak_left  = 0;    // Peak detection for left channel (24-bit aligned)
volatile int32_t peak_right = 0;
volatile int32_t peak_left_block  = 0;
volatile int32_t peak_right_block = 0;
volatile int32_t local_peak_left  = 0;
volatile int32_t local_peak_right = 0;

absolute_time_t last_sample_time = {0}; // Timestamp for VU meter timing

// Compressor gain reduction for VU meter
static int32_t comp_linear_gain_q24_l = 0;
static int32_t comp_linear_gain_q24_r = 0;

// Audio volume in Q0.16 format (0..65536)
static uint32_t volume_q16 = 0;

// Define I2S audio parameters
float sample_period_us = 0.0f;

// ============================================================================
// === Inline Helpers =========================================================
// ============================================================================

// Clamp 64-bit value to int32_t range (used carefully)
static inline __attribute__((always_inline)) int32_t clamp32(int64_t x) {
    if (x > INT32_MAX) return INT32_MAX;
    if (x < INT32_MIN) return INT32_MIN;
    return (int32_t)x;
}

// Clamp 32-bit value to 24-bit value 
static inline __attribute__((always_inline)) int32_t clamp24(int32_t x) {
    if (x > PEAK_MAX) x = PEAK_MAX;
    if (x < PEAK_MIN) x = PEAK_MIN;
    return (int32_t)x;
}

// ============================================================================
// === Audio Processing Functions =============================================
// ============================================================================

// === Filter structs ===
typedef struct {
    int32_t a_q24;
    int32_t state_l, state_r;
} OnePole;

typedef struct {
    OnePole hpf, lpf;
    int32_t gain_q24;
    int32_t s1_l, s2_l;
    int32_t s1_r, s2_r;
} BPFPair;

// Apply a 1-pole IIR filter
static inline int32_t apply_1pole_lpf(int32_t x, int32_t* state, int32_t a_q24) {
    int32_t diff = x - *state;
    *state += (int32_t)(((int64_t)diff * a_q24) >> 24);
    return *state;
}

// Apply a 1-pole HPF filter
static inline int32_t apply_1pole_hpf(int32_t x, int32_t* state, int32_t a_q24) {
    int32_t prev = *state;
    int32_t diff = x - prev;
    *state += (int32_t)(((int64_t)diff * a_q24) >> 24);
    return x - *state;
}

// Band Pass filter
static inline int32_t apply_1pole_bpf(int32_t x, BPFPair* f, int ch) {
    int32_t* s1 = (ch == 0) ? &f->hpf.state_l : &f->hpf.state_r;
    int32_t* s2 = (ch == 0) ? &f->lpf.state_l : &f->lpf.state_r;

    int32_t hp = apply_1pole_hpf(x, s1, f->hpf.a_q24);
    int32_t bp = apply_1pole_lpf(hp, s2, f->lpf.a_q24);

    return (f->gain_q24 == Q24_ONE) ? bp : qmul(bp, f->gain_q24);
}

// Band Stop filter
static inline int32_t apply_1pole_bsf(int32_t x, BPFPair* f, int ch) {
    int32_t* s1 = (ch == 0) ? &f->hpf.state_l : &f->hpf.state_r;
    int32_t* s2 = (ch == 0) ? &f->lpf.state_l : &f->lpf.state_r;

    int32_t hp = apply_1pole_hpf(x, s1, f->hpf.a_q24);
    int32_t bp = apply_1pole_lpf(hp, s2, f->lpf.a_q24);

    int32_t notch = x - bp;
    return qmul(notch, f->gain_q24);
}

// Track peak levels for clipping and VU meter (24-bit samples)
void process_audio_clipping(int32_t sample_left, int32_t sample_right, volatile int32_t* local_peak_left, volatile int32_t* local_peak_right) {
    int32_t abs_left = (sample_left < 0) ? -sample_left : sample_left;
    if (abs_left > *local_peak_left) *local_peak_left = abs_left;

    int32_t abs_right = (sample_right < 0) ? -sample_right : sample_right;
    if (abs_right > *local_peak_right) *local_peak_right = abs_right;
}

// Update volume from potentiometer (pot_value scaled 0..POT_MAX)
void update_volume_from_pot(void) {
    volume_q16 = ((uint32_t)pot_value[6] * Q16_ONE) / POT_MAX;
}

// Takes ~1% of core 0 CPU time at 48kHz
// Apply volume to one stereo sample pair (24-bit quality)
void process_audio_volume_sample(int32_t* inout_l, int32_t* inout_r) {
    *inout_l = multiply_q16(*inout_l, volume_q16);
    *inout_r = multiply_q16(*inout_r, volume_q16);
}

// ============================================================================
// === LFO Functions ==========================================================
// ============================================================================

// Modes
#define LFO_TRIANGLE         0
#define LFO_TRIANGLE_SMOOTH  1
#define LFO_SINE             2

// Input: 32-bit phase accumulator
// Output: Q16 LFO value (0..65535)
static inline uint32_t lfo_q16_shape(uint32_t phase, uint8_t mode) {
    uint32_t folded = (phase >> 15) & 0x1FFFF; // 17-bit folded phase
    if (folded >= 65536)
        folded = 131071 - folded; // triangle-like shape in [0..65535]

    if (mode == LFO_TRIANGLE) {
        return folded;
    }
    else if (mode == LFO_TRIANGLE_SMOOTH) {
        // Smoothstep: y = 3x^2 - 2x^3
        uint32_t x = folded; // Q16
        uint64_t x2 = ((uint64_t)x * x) >> 16;      // Q32 >> 16 = Q16
        uint64_t x3 = (x2 * x) >> 16;               // Q32 >> 16 = Q16
        uint64_t y = (3 * x2) - (2 * x3);           // Q16
        return (y > 65535) ? 65535 : (uint32_t)y;
    }
    else if (mode == LFO_SINE) {
        // Parabolic sine approx: y = 1 - 4(x - 0.5)^2
        int32_t x_q16 = (int32_t)folded - 32768;
        int64_t x2 = ((int64_t)x_q16 * x_q16) >> 15; // Q17
        int32_t y_q16 = 65535 - (int32_t)x2;
        return (y_q16 < 0) ? 0 : (uint32_t)y_q16;
    }

    // Default fallback (triangle)
    return folded;
}
// ============================================================================
// === Audio Effect Functions ================================================
// ============================================================================

#include <chorus.h>
#include <compressor.h>
#include <delay.h>
#include <distortion.h>
#include <eq.h>
#include <flanger.h>
#include <fuzz.h>
#include <overdrive.h>
#include <phaser.h>
#include <preamp.h>
#include <reverb.h>
#include <speaker_sim.h>
#include <tremolo.h>
#include <vibrato.h>
