/* var_conversion.h
 * Author: Milan Wendt
 * Date:   2025-09-03
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

#define Q31_ONE     0x7FFFFFFF
#define Q24_ONE     0x01000000
#define Q16_ONE     0x00010000
#define Q15_ONE     0x00008000

// ============================================================================
// === Potentiometer Conversion  ==============================================
// ============================================================================

// Convert pot to Q16
static inline uint32_t map_pot_to_q16(int32_t pot, uint32_t min_q16, uint32_t max_q16) {
    return min_q16 + ((uint64_t)pot * (max_q16 - min_q16)) / POT_MAX;
}

// Convert pot to Q24
static inline int32_t map_pot_to_q24(int32_t pot, int32_t min_q24, int32_t max_q24) {
    return min_q24 + ((int64_t)pot * (max_q24 - min_q24)) / POT_MAX;
}

// Convert pot to INT
static inline int32_t map_pot_to_int(int32_t pot, int32_t min_int, int32_t max_int) {    
    return min_int + ((int64_t)pot * (max_int - min_int)) / POT_MAX;
}

// Convert pot to EVEN INT
static inline int32_t map_pot_to_even_range(int32_t pot, int32_t min_even, int32_t max_even) {
    int steps = (max_even - min_even) / 2;
    int index = map_pot_to_int(pot, 0, steps);
    return min_even + 2 * index;
}

// Convert pot to FREQUENCY (float)
static inline float map_pot_to_freq(int pot, float min_hz, float max_hz) {
    return min_hz + ((float)pot / POT_MAX) * (max_hz - min_hz);
}

// ============================================================================
// === Format Conversion  =====================================================
// ============================================================================

// Convert float to Q16
static inline uint32_t float_to_q16(float x) {
    return (uint32_t)(x * Q16_ONE);
}

// Convert Q16 to float
static inline float q16_to_float(int32_t x) {
    return x / 65536.0f;
}

// Convert float to Q24
static inline int32_t float_to_q24(float x) {
    return (int32_t)(x * (1 << 24));
}

// Convert Q24 to float
static inline float q24_to_float(int32_t x) {
    return x / 16777216.0f;
}

// Fast approximate conversion from linear gain to Q24
static inline int32_t db_to_q24(float db) {
    // 20*log10(gain) = db → gain = 10^(db/20)
    float lin = powf(10.0f, db / 20.0f);
    return (int32_t)(lin * (1 << 24));
}

// One-pole alpha from Hz (run only on param updates)
static inline int32_t alpha_from_hz(float fc_hz) {
    if (fc_hz <= 0.0f) return 0;
    float a = 1.0f - expf(-2.0f * (float)M_PI * fc_hz / (float)SAMPLE_RATE);
    if (a < 0.0f) a = 0.0f; if (a > 1.0f) a = 1.0f;
    return float_to_q24(a);
}

// ============================================================================
// === Math & Conversion  =====================================================
// ============================================================================

// --- Convert ms to Q8.24 coefficient: a = exp(-1 / (tau * fs)) ---
static inline int32_t ms_to_coeff_q24(float ms, float fs) {
    float coeff = expf(-1.0f / (ms * 0.001f * fs));
    return float_to_q24(coeff);
}

// Q8.24 multiply with round-to-nearest
static inline int32_t qmul(int32_t a, int32_t b){
    int64_t p = (int64_t)a * b; 
    p += (p >= 0) ? (1LL<<23) : -(1LL<<23);
    return (int32_t)(p >> 24);
}

// === Lerp and multiply helpers ===

// Linear interpolation between a and b with frac in Q16
static inline int32_t lerp_fixed(int32_t a, int32_t b, uint32_t frac_q16) {
    return a + ((int64_t)(b - a) * frac_q16 >> 16);
}

// Fixed-point multiplication: a * b in Q16
static inline __attribute__((always_inline)) int32_t multiply_q16(int32_t a, uint32_t b) {
    return (int32_t)(((int64_t)a * b) >> 16);
}

// Fixed-point division: (num << 24) / den
static inline int32_t qdiv(int32_t num, int32_t den) {
    if (den == 0) return Q24_ONE;
    return (int32_t)(((int64_t)num << 24) / den);
}

// Frequency to Q24 coefficient for one-pole filters
static inline int32_t fc_to_q24(uint32_t fc, uint32_t fs) {
    if (fc >= fs / 2)
        return 0xFFFFFF; // Q24_ONE

    double normalized = (double)fc / fs;
    double coeff = 2.0 * sin(M_PI * normalized);
    return (int32_t)(coeff * (1 << 24) + 0.5);
}