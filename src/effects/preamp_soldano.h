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
#ifndef SLO_PREAMP_H
#define SLO_PREAMP_H

#include <stdint.h>
#include <stdbool.h>
#include <math.h>

/*  Soldano SLO-100 style preamp (Q8.24)
    Flow: In → InputPad → HPF → PreVol+Bright → V1A (hot)
          → Coupler HPF (cold-clipper tightness) → V2A (hot, asym)
          → CF-ish squish → Tone Proxy → Presence shelf → Post LPF → Master

    Controls (same map as your other amps):
      pot[0]=PreVol, pot[1]=Bass, pot[2]=Mid, pot[3]=Treble, pot[4]=Presence, pot[5]=Master

    Depends on your helpers: qmul, clamp24, float_to_q24, db_to_q24, alpha_from_hz,
      map_pot_to_q24, apply_1pole_lpf/hpf, storedPotValue[..].
*/

/* ========================== Compile-time knobs =========================== */
#ifndef SLO_ECO
#define SLO_ECO                 0        // 0 = use final post LPF (tames fizz, default ON)
#endif

#ifndef SLO_ECO_PRES
#define SLO_ECO_PRES            0        // 0 = dedicated presence shelf (default)
#endif

#ifndef SLO_USE_X5
#define SLO_USE_X5              1        // keep x^5 gating
#endif

#ifndef SLO_EFFECT_INDEX
#define SLO_EFFECT_INDEX        PREAMP_EFFECT_INDEX
#endif

#define SLO_INPUT_PAD_DB       (-9.0f)
#define SLO_STACK_MAKEUP_DB    (+18.0f)  // SLO stack loss is hefty
#define SLO_PREVOL_MIN_DB      (-42.0f)
#define SLO_STAGEA_GAIN        (+12.0f)
#define SLO_STAGEB_GAIN        (+14.0f)

/* Stage coefficients (aggressive, ear-tuned starting points) */
#define SLO_K3A                 (0.35f)
#define SLO_K5A                 (0.12f)
#define SLO_K3B                 (0.55f)
#define SLO_K5B                 (0.18f)

/* Asymmetry (stronger than Marshall; extra negative-lobe skew on Stage B) */
#define SLO_ASYM_A_BASE         (0.66f)
#define SLO_ASYM_A_DEPTH        (0.00f)

#define SLO_ASYM_B_BASE         (0.55f)
#define SLO_ASYM_B_DEPTH        (0.12f)

/* x^5 engages above this magnitude */
#define SLO_WS_X5_ON            (0.06f)

/* Envelope for Stage-B asym */
#define SLO_ENVB_HZ             (14.0f)
#define SLO_ENV_DECIM           2

/* Pre-Vol behavior */
#define SLO_PREVOL_TAPER        (1.25f)
#define SLO_PREVOL_TOP_BOOST_DB (+3.0f)

/* Bright mix cap (strong cap at low PreVol, but not ice-picky) */
#define SLO_BRIGHT_MAX_DB       (+5.0f)

/* ============================== Voicing ================================== */
typedef struct {
    float pre_hpf_Hz;      // rumble cut
    float cpl1_hz;         // coupler HPF before Stage A
    float cpl2_hz;         // coupler HPF before Stage B (set high for cold-clipper tightness)

    float bright_hz_min;   // bright corner at high PreVol
    float bright_hz_max;   // bright corner at low PreVol

    float bass_hz;
    float mid_hz;
    float treble_hz;
    float presence_hz;
    float post_lpf_Hz;

    float stageA_shape;
    float stageA_asym;
    float stageB_shape;
    float stageB_asym;
} slo_voice_t;

static const slo_voice_t SLO_VOICE = {
    .pre_hpf_Hz   = 30.0f,
    .cpl1_hz      = 15.0f,
    .cpl2_hz      = 680.0f,   // key to the SLO tight low end

    .bright_hz_min= 2500.0f,
    .bright_hz_max= 8000.0f,

    // SLO-ish tonestack centers
    .bass_hz      = 90.0f,
    .mid_hz       = 500.0f,
    .treble_hz    = 4500.0f,
    .presence_hz  = 3800.0f,
    .post_lpf_Hz  = 9000.0f,  // rein in fizz

    .stageA_shape = 0.28f, .stageA_asym = 1.18f,
    .stageB_shape = 0.95f, .stageB_asym = 1.55f,
};

/* ============================ Parameters/State ============================ */
static int32_t slo_prevol_q24        = 0x01000000; // pot[0]
static int32_t slo_master_q24        = 0x01000000; // pot[5]
static int32_t slo_bass_gain_q24     = 0x01000000; // pot[1]
static int32_t slo_mid_gain_q24      = 0x01000000; // pot[2]
static int32_t slo_treble_gain_q24   = 0x01000000; // pot[3]
static int32_t slo_presence_gain_q24 = 0x01000000; // pot[4]

static int32_t slo_input_pad_q24     = 0x01000000;
static int32_t slo_bright_mix_q24    = 0;          // +dB highs (0..+5 dB)
static int32_t slo_stack_makeup_q24  = 0x01000000;

/* Stage linear gains (hotter than others) */
static int32_t slo_stageA_gain_q24   = 0x01000000; // ~ +16..18 dB each
static int32_t slo_stageB_gain_q24   = 0x01000000;

/* Triode shaper coeffs (Q24) */
static int32_t slo_stageA_k3_q24     = 0;
static int32_t slo_stageA_k5_q24     = 0;
static int32_t slo_stageB_k3_q24     = 0;
static int32_t slo_stageB_k5_q24     = 0;

/* CF-ish squish (helps smooth top) */
static int32_t slo_cf_amount_q24     = 0;

/* One-pole alphas (Q24) */
static int32_t slo_pre_hpf_a_q24     = 0;
static int32_t slo_cpl1_a_q24        = 0;
static int32_t slo_bright_a_q24      = 0;
static int32_t slo_cpl2_a_q24        = 0;
static int32_t slo_bass_a_q24        = 0;
static int32_t slo_mid_a_q24         = 0;
static int32_t slo_treble_a_q24      = 0;
static int32_t slo_presence_a_q24    = 0;
static int32_t slo_post_lpf_a_q24    = 0;

/* Stage-B envelope alpha */
static int32_t slo_envB_a_q24        = 0;

/* States (stereo) */
static int32_t slo_pre_hpf_state_l=0, slo_pre_hpf_state_r=0;
static int32_t slo_cpl1_state_l=0,   slo_cpl1_state_r=0;
static int32_t slo_bright_state_l=0, slo_bright_state_r=0;
static int32_t slo_cpl2_state_l=0,   slo_cpl2_state_r=0;

static int32_t slo_bass_state_l=0,   slo_bass_state_r=0;
static int32_t slo_mid_lp_state_l=0, slo_mid_lp_state_r=0;
static int32_t slo_mid_hp_state_l=0, slo_mid_hp_state_r=0;
static int32_t slo_treble_state_l=0, slo_treble_state_r=0;

static int32_t slo_presence_state_l=0, slo_presence_state_r=0;
static int32_t slo_post_lpf_state_l=0, slo_post_lpf_state_r=0;

/* Stage-B env + decim */
static int32_t slo_envB_state_l=0, slo_envB_state_r=0;
static uint8_t slo_envB_decim_l=0, slo_envB_decim_r=0;

/* =============================== Nonlinears =============================== */
// Triode-ish: y = x - k3*x^3 + k5*x^5 (x^5 gated); sign-dependent asym via coeffs
static inline __attribute__((always_inline))
int32_t slo_triode_ws_35_asym_fast(int32_t x,
                                   int32_t k3_pos_q24, int32_t k5_pos_q24,
                                   int32_t k3_neg_q24, int32_t k5_neg_q24)
{
    if (x >  0x01000000) x =  0x01000000;
    if (x < -0x01000000) x = -0x01000000;

    int64_t x2 = ((int64_t)x * x) >> 24; // x^2
    int64_t x3 = (x2 * x) >> 24;         // x^3
#if SLO_USE_X5
    int64_t x5 = (x3 * x2) >> 24;        // x^5
#endif

    const int32_t k3 = (x >= 0) ? k3_pos_q24 : k3_neg_q24;
    int32_t term3 = (int32_t)(((int64_t)k3 * x3) >> 24);

    int32_t y = x - term3;

#if SLO_USE_X5
    int32_t ax  = (x >= 0) ? x : -x;
    if (ax > float_to_q24(SLO_WS_X5_ON)) {
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
int32_t slo_cathode_squish(int32_t x, int32_t amount_q24){
    if (x > 0){
        int32_t x2   = (int32_t)(((int64_t)x * x) >> 24);
        int32_t comp = qmul(amount_q24, x2);
        return x - comp;
    } else {
        return qmul(x, float_to_q24(0.965f)); // a hair more clamp than Marshall
    }
}

/* =============================== Core process ============================ */
static inline __attribute__((always_inline)) int32_t process_slo_channel(
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
    s = qmul(s, slo_input_pad_q24);

    // optional global HPF (off for CPU)
    // if (slo_pre_hpf_a_q24) s = apply_1pole_hpf(s, pre_hpf_state, slo_pre_hpf_a_q24);

    // Pre-vol input coupler
    s = apply_1pole_hpf(s, cpl1_state, slo_cpl1_a_q24);

    // Pre-Volume + Bright
    if (slo_bright_mix_q24){
        int32_t l = apply_1pole_lpf(s, bright_state, slo_bright_a_q24);
        int32_t h = s - l;
        int32_t base       = qmul(s, slo_prevol_q24);
        int32_t bright_add = qmul(qmul(h, slo_bright_mix_q24), slo_prevol_q24);
        s = base + bright_add;
    } else {
        s = qmul(s, slo_prevol_q24);
    }

    // ================= Stage A (hot) =================
    s = qmul(s, slo_stageA_gain_q24);

    int32_t asymA = float_to_q24(SLO_ASYM_A_BASE);
    int32_t k3A_neg = qmul(slo_stageA_k3_q24, asymA);
    int32_t k5A_neg = qmul(slo_stageA_k5_q24, asymA);
    s = slo_triode_ws_35_asym_fast(s,
            slo_stageA_k3_q24, slo_stageA_k5_q24,
            k3A_neg,           k5A_neg);

    // Key tightness: high HPF into Stage B
    s = apply_1pole_hpf(s, cpl2_state, slo_cpl2_a_q24);

    // ================= Stage B (hot, skewed) =================
    int32_t envB;
    if ( ((*envB_decim)++ & (SLO_ENV_DECIM-1)) == 0 ){
        int32_t s_abs = (s >= 0) ? s : -s;
        envB = apply_1pole_lpf(s_abs, envB_state, slo_envB_a_q24);
    } else {
        envB = *envB_state;
    }

    int32_t asymB = float_to_q24(SLO_ASYM_B_BASE);
    asymB += qmul(float_to_q24(SLO_ASYM_B_DEPTH), envB);

    int32_t k3B_neg = qmul(slo_stageB_k3_q24, asymB);
    int32_t k5B_neg = qmul(slo_stageB_k5_q24, asymB);

    s = qmul(s, slo_stageB_gain_q24);
    s = slo_triode_ws_35_asym_fast(s,
            slo_stageB_k3_q24, slo_stageB_k5_q24,
            k3B_neg,           k5B_neg);

    // CF-ish feel (more than Fender, less than Marshall)
    s = slo_cathode_squish(s, slo_cf_amount_q24);

    // ================= Tone proxy =================
    int32_t low      = apply_1pole_lpf(s, bass_state,   slo_bass_a_q24);
    int32_t low_out  = qmul(low, slo_bass_gain_q24);

    int32_t mid_bp   = apply_1pole_lpf( apply_1pole_hpf(s, mid_hp_state, slo_mid_a_q24),
                                        mid_lp_state, slo_mid_a_q24 );
    int32_t mid_out  = qmul(mid_bp, slo_mid_gain_q24);

    int32_t high_cmp = s - apply_1pole_lpf(s, treble_state, slo_treble_a_q24);
    int32_t high_out = qmul(high_cmp, slo_treble_gain_q24);

    int32_t mix32 = (int32_t)((int64_t)low_out + (int64_t)mid_out + (int64_t)high_out);

    // stack makeup
    mix32 = qmul(mix32, slo_stack_makeup_q24);

    // Presence shelf (post-stack)
#if SLO_ECO_PRES
    if (slo_presence_gain_q24 != 0x01000000){
        int32_t pres_delta = qmul(high_cmp, slo_presence_gain_q24 - 0x01000000);
        mix32 += pres_delta;
    }
#else
    if (slo_presence_gain_q24 != 0x01000000){
        int32_t pres_high  = mix32 - apply_1pole_lpf(mix32, presence_state, slo_presence_a_q24);
        int32_t pres_delta = qmul(pres_high, slo_presence_gain_q24 - 0x01000000);
        mix32 += pres_delta;
    }
#endif

#if !SLO_ECO
    if (slo_post_lpf_a_q24) mix32 = apply_1pole_lpf(mix32, post_lpf_state, slo_post_lpf_a_q24);
#endif

    // Master
    mix32 = qmul(mix32, slo_master_q24);
    return clamp24(mix32);
}

/* =============================== Public API ============================== */
static inline void process_audio_slo_sample(int32_t* inout_l, int32_t* inout_r, bool stereo){
    *inout_l = process_slo_channel(*inout_l,
        &slo_pre_hpf_state_l, &slo_cpl1_state_l, &slo_bright_state_l, &slo_cpl2_state_l,
        &slo_bass_state_l, &slo_mid_lp_state_l, &slo_mid_hp_state_l, &slo_treble_state_l,
        &slo_presence_state_l, &slo_post_lpf_state_l,
        &slo_envB_state_l, &slo_envB_decim_l);

    if(!stereo){
        *inout_r = *inout_l;
    } else {
        *inout_r = process_slo_channel(*inout_r,
            &slo_pre_hpf_state_r, &slo_cpl1_state_r, &slo_bright_state_r, &slo_cpl2_state_r,
            &slo_bass_state_r, &slo_mid_lp_state_r, &slo_mid_hp_state_r, &slo_treble_state_r,
            &slo_presence_state_r, &slo_post_lpf_state_r,
            &slo_envB_state_r, &slo_envB_decim_r);
    }
}

static inline void slo_preamp_process_block(int32_t* in_l, int32_t* in_r, size_t frames, bool stereo){
    for (size_t i=0;i<frames;i++){
        process_audio_slo_sample(&in_l[i], &in_r[i], stereo);
    }
}

/* =============================== Param load ============================== */
static inline void load_slo_params_from_memory(void){
    // 1) Filter alphas
    slo_input_pad_q24  = db_to_q24(SLO_INPUT_PAD_DB);
    slo_pre_hpf_a_q24  = alpha_from_hz(SLO_VOICE.pre_hpf_Hz);
    slo_cpl1_a_q24     = alpha_from_hz(SLO_VOICE.cpl1_hz);
    slo_cpl2_a_q24     = alpha_from_hz(SLO_VOICE.cpl2_hz);
    slo_bass_a_q24     = alpha_from_hz(SLO_VOICE.bass_hz);
    slo_mid_a_q24      = alpha_from_hz(SLO_VOICE.mid_hz);
    slo_treble_a_q24   = alpha_from_hz(SLO_VOICE.treble_hz);
#if !SLO_ECO_PRES
    slo_presence_a_q24 = alpha_from_hz(SLO_VOICE.presence_hz);
#else
    slo_presence_a_q24 = 0;
#endif

#if !SLO_ECO
    slo_post_lpf_a_q24 = alpha_from_hz(SLO_VOICE.post_lpf_Hz);
#else
    slo_post_lpf_a_q24 = 0;
#endif

    // Stage-B envelope LPF
    slo_envB_a_q24     = alpha_from_hz(SLO_ENVB_HZ);

    // 2) Stage linear gains (hot)
    slo_stageA_gain_q24 = db_to_q24(SLO_STAGEA_GAIN);
    slo_stageB_gain_q24 = db_to_q24(SLO_STAGEA_GAIN);
    slo_stack_makeup_q24= db_to_q24(SLO_STACK_MAKEUP_DB);

    // 3) Triode shaper coeffs
    slo_stageA_k3_q24 = float_to_q24(SLO_K3A);
    slo_stageA_k5_q24 = float_to_q24(SLO_K5A);
    slo_stageB_k3_q24 = float_to_q24(SLO_K3B);
    slo_stageB_k5_q24 = float_to_q24(SLO_K5B);

    // 4) CF squish amount (firm)
    slo_cf_amount_q24 = float_to_q24(0.20f + 0.12f * (SLO_VOICE.stageB_asym - 1.3f));

    // 5) Pots → controls
    int32_t pot;

    // Pre-Volume (hot taper + top boost)
    pot = storedPotValue[SLO_EFFECT_INDEX][0];
    float p = (float)pot / 4095.0f;
    float t = powf(p, SLO_PREVOL_TAPER);

    float prevol_db = SLO_PREVOL_MIN_DB + (0.0f - SLO_PREVOL_MIN_DB) * t;
    prevol_db += SLO_PREVOL_TOP_BOOST_DB * powf(p, 6.0f);
    slo_prevol_q24 = db_to_q24(prevol_db);

    // Bright mix stronger when pre-vol is low; cap at SLO_BRIGHT_MAX_DB
    int32_t prevol01 = float_to_q24(powf(p, SLO_PREVOL_TAPER));
    int32_t inv01    = 0x01000000 - prevol01;
    slo_bright_mix_q24 = qmul(inv01, db_to_q24(SLO_BRIGHT_MAX_DB) - 0x01000000);

    // Bright corner tracks pre-vol
    float bright_fc = SLO_VOICE.bright_hz_min +
                      (SLO_VOICE.bright_hz_max - SLO_VOICE.bright_hz_min) * (1.0f - p);
    slo_bright_a_q24 = alpha_from_hz(bright_fc);

    // Tone stack gains (same ranges)
    pot = storedPotValue[SLO_EFFECT_INDEX][1];
    slo_bass_gain_q24   = map_pot_to_q24(pot, db_to_q24(-12.0f), db_to_q24(+6.0f));
    pot = storedPotValue[SLO_EFFECT_INDEX][2];
    slo_mid_gain_q24    = map_pot_to_q24(pot, db_to_q24(-12.0f), db_to_q24(+12.0f));
    pot = storedPotValue[SLO_EFFECT_INDEX][3];
    slo_treble_gain_q24 = map_pot_to_q24(pot, db_to_q24(-12.0f), db_to_q24(+6.0f));

    // Presence: 0..+8 dB
    pot = storedPotValue[SLO_EFFECT_INDEX][4];
    slo_presence_gain_q24 = map_pot_to_q24(pot, db_to_q24(0.0f), db_to_q24(+8.0f));

    // Master: −3..+22 dB
    pot = storedPotValue[SLO_EFFECT_INDEX][5];
    slo_master_q24 = map_pot_to_q24(pot, db_to_q24(-3.0f), db_to_q24(+22.0f));

    // 6) Reset states (avoid zipper)
    slo_pre_hpf_state_l=slo_pre_hpf_state_r=0;
    slo_cpl1_state_l=slo_cpl1_state_r=0; slo_bright_state_l=slo_bright_state_r=0;
    slo_cpl2_state_l=slo_cpl2_state_r=0;

    slo_bass_state_l=slo_bass_state_r=0;
    slo_mid_lp_state_l=slo_mid_lp_state_r=0; slo_mid_hp_state_l=slo_mid_hp_state_r=0;
    slo_treble_state_l=slo_treble_state_r=0;

    slo_presence_state_l=slo_presence_state_r=0;
    slo_post_lpf_state_l=slo_post_lpf_state_r=0;

    slo_envB_state_l=slo_envB_state_r=0;
    slo_envB_decim_l=slo_envB_decim_r=0;
}

#endif // SLO_PREAMP_H
