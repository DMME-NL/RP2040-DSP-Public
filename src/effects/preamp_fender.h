/* 
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
#ifndef FENDER_PREAMP_H
#define FENDER_PREAMP_H

#include <stdint.h>
#include <stdbool.h>
#include <math.h>

/*  Fender Blackface-style preamp (Q8.24)
    Flow: In → InputPad → HPF → PreVol+Bright → V1A → Coupler HPF → V2A (cleaner)
          → mild cathode-ish squish → Tone Proxy → Presence shelf → Post LPF → Master

    Notes:
    - Same control semantics as your Marshall file (pots 0..5).
    - Uses your helpers: qmul, clamp24, float_to_q24, db_to_q24, alpha_from_hz,
      map_pot_to_q24, apply_1pole_lpf/hpf, storedPotValue[..].
*/

/* ========================== Compile-time knobs =========================== */
#ifndef FEND_ECO
#define FEND_ECO                1        // 1 = skip final post LPF
#endif

#ifndef FEND_ECO_PRES
#define FEND_ECO_PRES           0        // 0 = dedicated presence shelf (default)
#endif

#ifndef FEND_USE_X5
#define FEND_USE_X5             1        // keep x^5 gating (light)
#endif

#ifndef FENDER_EFFECT_INDEX
// Fallback: use your generic PREAMP_EFFECT_INDEX unless you override per-effect
#define FENDER_EFFECT_INDEX     PREAMP_EFFECT_INDEX
#endif

#define FEND_INPUT_PAD_DB      (-6.0f)   // Fender front end expects modest pad
#define FEND_STACK_MAKEUP_DB   (+15.0f)  // plate-driven stack is lossy
#define FEND_PREVOL_MIN_DB     (-45.0f)
#define FEND_STAGEA_GAIN       (+8.5f)
#define FEND_STAGEB_GAIN       (+9.5f)

/* Stage coefficients (cleaner than Marshall; ear-tuned starting points) */
#define FEND_K3A                (0.18f)
#define FEND_K5A                (0.05f)
#define FEND_K3B                (0.26f)
#define FEND_K5B                (0.06f)

/* Asymmetry (gentler than Marshall) */
#define FEND_ASYM_A_BASE        (0.80f)
#define FEND_ASYM_A_DEPTH       (0.00f)

#define FEND_ASYM_B_BASE        (0.75f)
#define FEND_ASYM_B_DEPTH       (0.05f)

/* Enable x^5 only above this magnitude */
#define FEND_WS_X5_ON           (0.10f)

/* Envelope for Stage-B asym */
#define FEND_ENVB_HZ            (8.0f)
#define FEND_ENV_DECIM          2

/* Pre-Vol behavior */
#define FEND_PREVOL_TAPER       (1.6f)
#define FEND_PREVOL_TOP_BOOST_DB (+0.0f)

/* Bright mix cap (stronger at low PreVol) */
#define FEND_BRIGHT_MAX_DB      (+6.0f)

/* ============================== Voicing ================================== */
typedef struct {
    float pre_hpf_Hz;      // rumble cut
    float cpl1_hz;         // coupler HPF before Stage A
    float cpl2_hz;         // coupler HPF before Stage B

    float bright_hz_min;   // bright corner at high PreVol
    float bright_hz_max;   // bright corner at low PreVol

    float bass_hz;
    float mid_hz;
    float treble_hz;
    float presence_hz;
    float post_lpf_Hz;

    // keep for compatibility / future retunes
    float stageA_shape;
    float stageA_asym;
    float stageB_shape;
    float stageB_asym;
} fender_voice_t;

static const fender_voice_t FEND_VOICE = {
    .pre_hpf_Hz   = 25.0f,
    .cpl1_hz      = 10.0f,
    .cpl2_hz      = 30.0f,

    // bright cap effect: pronounced at low PreVol, less at high
    .bright_hz_min= 1800.0f,
    .bright_hz_max= 7000.0f,

    // Blackface-ish tone proxy centers
    .bass_hz      = 80.0f,
    .mid_hz       = 400.0f,
    .treble_hz    = 3500.0f,
    .presence_hz  = 2500.0f,
    .post_lpf_Hz  = 10000.0f,

    .stageA_shape = 0.15f, .stageA_asym = 1.05f,
    .stageB_shape = 0.60f, .stageB_asym = 1.25f,
};

/* ============================ Parameters/State ============================ */
/* Pots (same mapping as Marshall) */
static int32_t fnd_prevol_q24        = 0x01000000; // pot[0]
static int32_t fnd_master_q24        = 0x01000000; // pot[5]
static int32_t fnd_bass_gain_q24     = 0x01000000; // pot[1]
static int32_t fnd_mid_gain_q24      = 0x01000000; // pot[2]
static int32_t fnd_treble_gain_q24   = 0x01000000; // pot[3]
static int32_t fnd_presence_gain_q24 = 0x01000000; // pot[4]

/* Fixed & derived */
static int32_t fnd_input_pad_q24     = 0x01000000;
static int32_t fnd_bright_mix_q24    = 0;          // +dB highs (0..+6 dB)
static int32_t fnd_stack_makeup_q24  = 0x01000000;

/* Stage linear gains (a bit tamer than Marshall) */
static int32_t fnd_stageA_gain_q24   = 0x01000000; // ~ +10..11 dB
static int32_t fnd_stageB_gain_q24   = 0x01000000;

/* Triode shaper coeffs (Q24) */
static int32_t fnd_stageA_k3_q24     = 0;
static int32_t fnd_stageA_k5_q24     = 0;
static int32_t fnd_stageB_k3_q24     = 0;
static int32_t fnd_stageB_k5_q24     = 0;

/* Cathode-ish squish (milder) */
static int32_t fnd_cf_amount_q24     = 0;

/* One-pole alphas (Q24) */
static int32_t fnd_pre_hpf_a_q24     = 0;
static int32_t fnd_cpl1_a_q24        = 0;
static int32_t fnd_bright_a_q24      = 0;
static int32_t fnd_cpl2_a_q24        = 0;
static int32_t fnd_bass_a_q24        = 0;
static int32_t fnd_mid_a_q24         = 0;
static int32_t fnd_treble_a_q24      = 0;
static int32_t fnd_presence_a_q24    = 0;
static int32_t fnd_post_lpf_a_q24    = 0;

/* Stage-B envelope alpha */
static int32_t fnd_envB_a_q24        = 0;

/* States (stereo) */
static int32_t fnd_pre_hpf_state_l=0, fnd_pre_hpf_state_r=0;
static int32_t fnd_cpl1_state_l=0,   fnd_cpl1_state_r=0;
static int32_t fnd_bright_state_l=0, fnd_bright_state_r=0;
static int32_t fnd_cpl2_state_l=0,   fnd_cpl2_state_r=0;

static int32_t fnd_bass_state_l=0,   fnd_bass_state_r=0;
static int32_t fnd_mid_lp_state_l=0, fnd_mid_lp_state_r=0;
static int32_t fnd_mid_hp_state_l=0, fnd_mid_hp_state_r=0;
static int32_t fnd_treble_state_l=0, fnd_treble_state_r=0;

static int32_t fnd_presence_state_l=0, fnd_presence_state_r=0;
static int32_t fnd_post_lpf_state_l=0, fnd_post_lpf_state_r=0;

/* Stage-B envelope states + decim counters */
static int32_t fnd_envB_state_l=0, fnd_envB_state_r=0;
static uint8_t fnd_envB_decim_l=0, fnd_envB_decim_r=0;

/* =============================== Nonlinears =============================== */
// Triode-ish: y = x - k3*x^3 + k5*x^5 (x^5 gated); sign-dependent asym via coeffs
static inline __attribute__((always_inline))
int32_t fnd_triode_ws_35_asym_fast(int32_t x,
                                   int32_t k3_pos_q24, int32_t k5_pos_q24,
                                   int32_t k3_neg_q24, int32_t k5_neg_q24)
{
    if (x >  0x01000000) x =  0x01000000;
    if (x < -0x01000000) x = -0x01000000;

    int64_t x2 = ((int64_t)x * x) >> 24; // x^2
    int64_t x3 = (x2 * x) >> 24;         // x^3
#if FEND_USE_X5
    int64_t x5 = (x3 * x2) >> 24;        // x^5
#endif

    const int32_t k3 = (x >= 0) ? k3_pos_q24 : k3_neg_q24;
    int32_t term3 = (int32_t)(((int64_t)k3 * x3) >> 24);

    int32_t y = x - term3;

#if FEND_USE_X5
    int32_t ax  = (x >= 0) ? x : -x;
    if (ax > float_to_q24(FEND_WS_X5_ON)) {
        const int32_t k5 = (x >= 0) ? k5_pos_q24 : k5_neg_q24;
        int32_t term5 = (int32_t)(((int64_t)k5 * x5) >> 24);
        y += term5;
    }
#endif

    if (y >  0x01000000) y =  0x01000000;
    if (y < -0x01000000) y = -0x01000000;
    return y;
}

static inline __attribute__((always_inline))
int32_t fnd_cathode_squish(int32_t x, int32_t amount_q24){
    if (x > 0){
        int32_t x2   = (int32_t)(((int64_t)x * x) >> 24);
        int32_t comp = qmul(amount_q24, x2);
        return x - comp;
    } else {
        return qmul(x, float_to_q24(0.985f)); // slightly less pull than Marshall
    }
}

/* =============================== Core process ============================ */
static inline __attribute__((always_inline)) int32_t process_fender_channel(
    int32_t s,
    // filters
    int32_t* pre_hpf_state,
    int32_t* cpl1_state, int32_t* bright_state,
    int32_t* cpl2_state,
    int32_t* bass_state, int32_t* mid_lp_state, int32_t* mid_hp_state, int32_t* treble_state,
    int32_t* presence_state,
    int32_t* post_lpf_state,
    // env (for Stage-B asym scaling)
    int32_t* envB_state, uint8_t* envB_decim
){
    // Input pad
    s = qmul(s, fnd_input_pad_q24);

    // optional global HPF (off by default for CPU)
    // if (fnd_pre_hpf_a_q24) s = apply_1pole_hpf(s, pre_hpf_state, fnd_pre_hpf_a_q24);

    // Pre-vol input coupler
    s = apply_1pole_hpf(s, cpl1_state, fnd_cpl1_a_q24);

    // Pre-Volume + Bright
    if (fnd_bright_mix_q24){
        int32_t l = apply_1pole_lpf(s, bright_state, fnd_bright_a_q24);
        int32_t h = s - l;
        int32_t base       = qmul(s, fnd_prevol_q24);
        int32_t bright_add = qmul(qmul(h, fnd_bright_mix_q24), fnd_prevol_q24);
        s = base + bright_add;
    } else {
        s = qmul(s, fnd_prevol_q24);
    }

    // ================= Stage A =================
    s = qmul(s, fnd_stageA_gain_q24);

    // Mild asym, mostly constant
    int32_t asymA = float_to_q24(FEND_ASYM_A_BASE);
    int32_t k3A_neg = qmul(fnd_stageA_k3_q24, asymA);
    int32_t k5A_neg = qmul(fnd_stageA_k5_q24, asymA);
    s = fnd_triode_ws_35_asym_fast(s,
            fnd_stageA_k3_q24, fnd_stageA_k5_q24,
            k3A_neg,           k5A_neg);

    // Coupler into Stage B
    s = apply_1pole_hpf(s, cpl2_state, fnd_cpl2_a_q24);

    // ================= Stage B =================
    // env (abs of input to this stage)
    int32_t envB;
    if ( ((*envB_decim)++ & (FEND_ENV_DECIM-1)) == 0 ){
        int32_t s_abs = (s >= 0) ? s : -s;
        envB = apply_1pole_lpf(s_abs, envB_state, fnd_envB_a_q24);
    } else {
        envB = *envB_state;
    }

    int32_t asymB = float_to_q24(FEND_ASYM_B_BASE);
    asymB += qmul(float_to_q24(FEND_ASYM_B_DEPTH), envB);

    int32_t k3B_neg = qmul(fnd_stageB_k3_q24, asymB);
    int32_t k5B_neg = qmul(fnd_stageB_k5_q24, asymB);

    s = qmul(s, fnd_stageB_gain_q24);
    s = fnd_triode_ws_35_asym_fast(s,
            fnd_stageB_k3_q24, fnd_stageB_k5_q24,
            k3B_neg,           k5B_neg);

    // gentle CF feel
    s = fnd_cathode_squish(s, fnd_cf_amount_q24);

    // ================= Tone proxy =================
    int32_t low      = apply_1pole_lpf(s, bass_state,   fnd_bass_a_q24);
    int32_t low_out  = qmul(low, fnd_bass_gain_q24);

    int32_t mid_bp   = apply_1pole_lpf( apply_1pole_hpf(s, mid_hp_state, fnd_mid_a_q24),
                                        mid_lp_state, fnd_mid_a_q24 );
    int32_t mid_out  = qmul(mid_bp, fnd_mid_gain_q24);

    int32_t high_cmp = s - apply_1pole_lpf(s, treble_state, fnd_treble_a_q24);
    int32_t high_out = qmul(high_cmp, fnd_treble_gain_q24);

    int32_t mix32 = (int32_t)((int64_t)low_out + (int64_t)mid_out + (int64_t)high_out);

    // stack loss makeup
    mix32 = qmul(mix32, fnd_stack_makeup_q24);

    // Presence shelf (post-stack)
#if FEND_ECO_PRES
    if (fnd_presence_gain_q24 != 0x01000000){
        int32_t pres_delta = qmul(high_cmp, fnd_presence_gain_q24 - 0x01000000);
        mix32 += pres_delta;
    }
#else
    if (fnd_presence_gain_q24 != 0x01000000){
        int32_t pres_high  = mix32 - apply_1pole_lpf(mix32, presence_state, fnd_presence_a_q24);
        int32_t pres_delta = qmul(pres_high, fnd_presence_gain_q24 - 0x01000000);
        mix32 += pres_delta;
    }
#endif

#if !FEND_ECO
    if (fnd_post_lpf_a_q24) mix32 = apply_1pole_lpf(mix32, post_lpf_state, fnd_post_lpf_a_q24);
#endif

    // Master
    mix32 = qmul(mix32, fnd_master_q24);
    return clamp24(mix32);
}

/* =============================== Public API ============================== */
static inline void process_audio_fender_sample(int32_t* inout_l, int32_t* inout_r, bool stereo){
    *inout_l = process_fender_channel(*inout_l,
        &fnd_pre_hpf_state_l, &fnd_cpl1_state_l, &fnd_bright_state_l, &fnd_cpl2_state_l,
        &fnd_bass_state_l, &fnd_mid_lp_state_l, &fnd_mid_hp_state_l, &fnd_treble_state_l,
        &fnd_presence_state_l, &fnd_post_lpf_state_l,
        &fnd_envB_state_l, &fnd_envB_decim_l);

    if(!stereo){
        *inout_r = *inout_l;
    } else {
        *inout_r = process_fender_channel(*inout_r,
            &fnd_pre_hpf_state_r, &fnd_cpl1_state_r, &fnd_bright_state_r, &fnd_cpl2_state_r,
            &fnd_bass_state_r, &fnd_mid_lp_state_r, &fnd_mid_hp_state_r, &fnd_treble_state_r,
            &fnd_presence_state_r, &fnd_post_lpf_state_r,
            &fnd_envB_state_r, &fnd_envB_decim_r);
    }
}

static inline void fender_preamp_process_block(int32_t* in_l, int32_t* in_r, size_t frames, bool stereo){
    for (size_t i=0;i<frames;i++){
        process_audio_fender_sample(&in_l[i], &in_r[i], stereo);
    }
}

/* =============================== Param load ============================== */
static inline void load_fender_params_from_memory(void){
    // 1) Filter alphas
    fnd_input_pad_q24  = db_to_q24(FEND_INPUT_PAD_DB);
    fnd_pre_hpf_a_q24  = alpha_from_hz(FEND_VOICE.pre_hpf_Hz);
    fnd_cpl1_a_q24     = alpha_from_hz(FEND_VOICE.cpl1_hz);
    fnd_cpl2_a_q24     = alpha_from_hz(FEND_VOICE.cpl2_hz);
    fnd_bass_a_q24     = alpha_from_hz(FEND_VOICE.bass_hz);
    fnd_mid_a_q24      = alpha_from_hz(FEND_VOICE.mid_hz);
    fnd_treble_a_q24   = alpha_from_hz(FEND_VOICE.treble_hz);
#if !FEND_ECO_PRES
    fnd_presence_a_q24 = alpha_from_hz(FEND_VOICE.presence_hz);
#else
    fnd_presence_a_q24 = 0;
#endif

#if !FEND_ECO
    fnd_post_lpf_a_q24 = alpha_from_hz(FEND_VOICE.post_lpf_Hz);
#else
    fnd_post_lpf_a_q24 = 0;
#endif

    // Stage-B envelope LPF
    fnd_envB_a_q24     = alpha_from_hz(FEND_ENVB_HZ);

    // 2) Stage linear gains
    fnd_stageA_gain_q24 = db_to_q24(FEND_STAGEA_GAIN);
    fnd_stageB_gain_q24 = db_to_q24(FEND_STAGEB_GAIN);
    fnd_stack_makeup_q24= db_to_q24(FEND_STACK_MAKEUP_DB);

    // 3) Triode shaper coeffs
    fnd_stageA_k3_q24 = float_to_q24(FEND_K3A);
    fnd_stageA_k5_q24 = float_to_q24(FEND_K5A);
    fnd_stageB_k3_q24 = float_to_q24(FEND_K3B);
    fnd_stageB_k5_q24 = float_to_q24(FEND_K5B);

    // 4) CF squish amount (mild)
    fnd_cf_amount_q24 = float_to_q24(0.12f + 0.10f * (FEND_VOICE.stageB_asym - 1.1f));

    // 5) Pots → controls (same ranges as Marshall)
    int32_t pot;

    // Pre-Volume (audio-ish taper; bottom deep cut, no top boost)
    pot = storedPotValue[FENDER_EFFECT_INDEX][0];
    float p = (float)pot / 4095.0f;
    float t = powf(p, FEND_PREVOL_TAPER);

    float prevol_db = FEND_PREVOL_MIN_DB + (0.0f - FEND_PREVOL_MIN_DB) * t;
    prevol_db += FEND_PREVOL_TOP_BOOST_DB * powf(p, 6.0f);
    fnd_prevol_q24 = db_to_q24(prevol_db);

    // Bright mix stronger when pre-vol is low; cap at FEND_BRIGHT_MAX_DB
    int32_t prevol01 = float_to_q24(powf(p, FEND_PREVOL_TAPER));
    int32_t inv01    = 0x01000000 - prevol01;
    fnd_bright_mix_q24 = qmul(inv01, db_to_q24(FEND_BRIGHT_MAX_DB) - 0x01000000);

    // Bright corner tracks pre-vol (lower corner overall than Marshall)
    float bright_fc = FEND_VOICE.bright_hz_min +
                      (FEND_VOICE.bright_hz_max - FEND_VOICE.bright_hz_min) * (1.0f - p);
    fnd_bright_a_q24 = alpha_from_hz(bright_fc);

    // Tone stack gains
    pot = storedPotValue[FENDER_EFFECT_INDEX][1];
    fnd_bass_gain_q24   = map_pot_to_q24(pot, db_to_q24(-12.0f), db_to_q24(+6.0f));
    pot = storedPotValue[FENDER_EFFECT_INDEX][2];
    fnd_mid_gain_q24    = map_pot_to_q24(pot, db_to_q24(-12.0f), db_to_q24(+12.0f));
    pot = storedPotValue[FENDER_EFFECT_INDEX][3];
    fnd_treble_gain_q24 = map_pot_to_q24(pot, db_to_q24(-12.0f), db_to_q24(+6.0f));

    // Presence: 0..+8 dB (same)
    pot = storedPotValue[FENDER_EFFECT_INDEX][4];
    fnd_presence_gain_q24 = map_pot_to_q24(pot, db_to_q24(0.0f), db_to_q24(+8.0f));

    // Master: −3..+22 dB (same)
    pot = storedPotValue[FENDER_EFFECT_INDEX][5];
    fnd_master_q24 = map_pot_to_q24(pot, db_to_q24(-3.0f), db_to_q24(+22.0f));

    // 6) Reset states (avoid zipper)
    fnd_pre_hpf_state_l=fnd_pre_hpf_state_r=0;
    fnd_cpl1_state_l=fnd_cpl1_state_r=0; fnd_bright_state_l=fnd_bright_state_r=0;
    fnd_cpl2_state_l=fnd_cpl2_state_r=0;

    fnd_bass_state_l=fnd_bass_state_r=0;
    fnd_mid_lp_state_l=fnd_mid_lp_state_r=0; fnd_mid_hp_state_l=fnd_mid_hp_state_r=0;
    fnd_treble_state_l=fnd_treble_state_r=0;

    fnd_presence_state_l=fnd_presence_state_r=0;
    fnd_post_lpf_state_l=fnd_post_lpf_state_r=0;

    fnd_envB_state_l=fnd_envB_state_r=0;
    fnd_envB_decim_l=fnd_envB_decim_r=0;
}

#endif // FENDER_PREAMP_H
