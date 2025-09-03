/* ui_variables.h
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

// UI state enumeration
typedef enum {
    UI_HOME,
    UI_POT,
    UI_VU_IN,
    UI_VU_OUT,
    UI_VU_GAIN,
    UI_EFFECT_LIST,
    UI_DELAY_MODE_MENU,
    UI_DELAY_FRACTION_L_MENU,
    UI_DELAY_FRACTION_R_MENU,
    UI_STEREO_MODE_MENU,
    UI_CHORUS_MODE_MENU,
    UI_PREAMP_SELECTION
} UIState;

// VU state enumeration
typedef enum {
    VU_INPUT,
    VU_OUTPUT,
    VU_GAIN
} VUmeterState;

// preamp options 
const char* preamp_names[] = {
    "FENDER",
    "VOX",
    "MARSHALL",
    "SOLDANO"
};

typedef enum {
    FENDER,
    VOX_AC,
    MARSHALL,
    SOLDANO
} preamp;


// stereo modes
const char* stereo_mode_names[] = {
    "STEREO",
    "MONO"
};

typedef enum {
    FX_STEREO,
    FX_MONO
} FXmode;

// delay modes
typedef enum {
    DELAY_MODE_PARALLEL = 0,  // Standard L/R independent
    DELAY_MODE_PINGPONG,      // Mono input, L <-> R bounce
    DELAY_MODE_CROSS,          // L feeds R, R feeds L
    DELAY_MODE_MIXED       // Mixed feedback from both channels
} DelayMode;

const char* delay_mode_names[] = {
    "PARALLEL",
    "PING-PONG",
    "CROSSED",
    "MIXED"
};

// Chorus modes
typedef enum {
    STEREO_3,
    STEREO_2,
    MONO
} ChorusMode;

const char* chorus_mode_names[] = {
    "3-STEREO 120",
    "2-STEREO 180",
    "1-MONO"
};

#define NUM_DELAY_MODES  (sizeof(delay_mode_names) / sizeof(delay_mode_names[0]))
#define NUM_STEREO_MODES (sizeof(stereo_mode_names) / sizeof(stereo_mode_names[0]))
#define NUM_CHORUS_MODES (sizeof(chorus_mode_names) / sizeof(chorus_mode_names[0]))
#define NUM_PREAMPS      (sizeof(preamp_names) / sizeof(preamp_names[0]))

static DelayMode selected_delay_mode = DELAY_MODE_PARALLEL;
static preamp selected_preamp_style  = MARSHALL;
static FXmode selected_chorus_mode   = STEREO_3;
static FXmode selected_phaser_mode   = FX_STEREO;
static FXmode selected_flanger_mode  = FX_STEREO;
static FXmode selected_tremolo_mode  = FX_STEREO;
static FXmode selected_vibrato_mode  = FX_STEREO;

// Delay Fractions
typedef enum {
    QUARTER,
    DOTTED_EIGHTH,
    QUARTER_TRIPLET,
    EIGHTH,
    EIGHTH_TRIPLET,
    SIXTEENTH
} DelayFraction;

// Array of fractions as floats
static const float delay_fraction_float[] = {
    1.0f,    // QUARTER
    0.75f,   // DOTTED_EIGHTH
    0.667f,  // QUARTER_TRIPLET
    0.5f,    // EIGHTH
    0.333f,  // EIGHTH_TRIPLET
    0.25f    // SIXTEENTH
};

// Array of fractions as user-friendly names
const char* delay_fraction_name[] = {
    "1/1",  // QUARTER (whole beat)
    "3/4",  // DOTTED EIGHTH
    "2/3",  // QUARTER TRIPLET
    "1/2",  // EIGHTH
    "1/3",  // EIGHTH TRIPLET
    "1/4"   // SIXTEENTH
};

#define NUM_FRACTIONS (sizeof(delay_fraction_float) / sizeof(delay_fraction_float[0]))

DelayFraction delay_time_fraction_l = QUARTER;
DelayFraction delay_time_fraction_r = DOTTED_EIGHTH;

// List of all effects
static const char* allEffects[] = {
    "CHORUS",       // CHRS_EFFECT_INDEX
    "COMPRESSOR",   // COMP_EFFECT_INDEX
    "DELAY",        // DELAY_EFFECT_INDEX
    "DISTORTION",   // DS_EFFECT_INDEX
    "EQ",           // EQ_EFFECT_INDEX
    "FLANGER",      // FLNG_EFFECT_INDEX
    "FUZZ",         // FZ_EFFECT_INDEX
    "OVERDRIVE",    // OD_EFFECT_INDEX
    "PHASER",       // PHSR_EFFECT_INDEX
    "PREAMP",       // PREAMP_EFFECT_INDEX
    "REVERB",       // REVB_EFFECT_INDEX
    "CAB SIM",      // CAB_SIM_EFFECT_INDEX
    "TREMOLO",      // TREM_EFFECT_INDEX
    "VIBRATO"       // VIBR_EFFECT_INDEX
};

enum {
    CHRS_EFFECT_INDEX,      // 0  CHORUS
    COMP_EFFECT_INDEX,      // 1  COMPRESSOR
    DELAY_EFFECT_INDEX,     // 2  DELAY
    DS_EFFECT_INDEX,        // 3  DISTORTION
    EQ_EFFECT_INDEX,        // 4  EQ
    FLNG_EFFECT_INDEX,      // 5  FLANGER
    FZ_EFFECT_INDEX,        // 6  FUZZ
    OD_EFFECT_INDEX,        // 7  OVERDRIVE
    PHSR_EFFECT_INDEX,      // 8  PHASER
    PREAMP_EFFECT_INDEX,    // 9  PREAMP
    REVB_EFFECT_INDEX,      // 10 REVERB
    CAB_SIM_EFFECT_INDEX,   // 11 CABINET SIMULATION
    TREM_EFFECT_INDEX,      // 12 TREMOLO
    VIBR_EFFECT_INDEX,      // 13 VIBRATO
    NUM_EFFECTS             // 14 Total number of effects
};

#define NUM_EFFECTS (sizeof(allEffects) / sizeof(allEffects[0]))
#define NUM_FUNC_POTS 6

static const char potLabelSets[NUM_EFFECTS][NUM_FUNC_POTS][10] = {
    // Example pot labels for each effect
    { "Speed",      "Depth",    "-",        "Mix",      "LPF",      "Volume" },   // 0  CHORUS      [V]
    { "Threshold",  "Ratio",    "Attack",   "Release",  "-",        "Volume" },   // 1  COMPRESSOR  [V]
    { "L Delay",    "R Delay",  "Feedback", "Mix",      "LPF",      "Volume" },   // 2  DELAY       [V]
    { "Gain",       "Bass",     "Mid",      "Frequency","Treble",   "Volume" },   // 3  DISTORTION  [V]
    { "Bass",       "Mid",      "Frequency","Treble",   "LPF",      "Volume" },   // 4  EQ          [V]
    { "Speed",      "Depth",    "Feedback", "Mix",      "LPF",      "Volume" },   // 5  FLANGER     [V]
    { "Gain",       "Bass",     "Mid",      "Frequency","Treble",   "Volume" },   // 6  FUZZ        [V]
    { "Gain",       "Bass",     "Mid",      "Frequency","Treble",   "Volume" },   // 7  OVERDRIVE   [V]
    { "Speed",      "Low",      "High",     "Feedback", "Mix",      "Volume" },   // 8  PHASER      [V]
    { "Gain",       "Bass",     "Mid",      "Treble",   "Precense", "Volume" },   // 9  PREAMP      [V]
    { "Mix",        "Decay",    "Diffuse",  "Dampig",   "Size",     "Volume" },   // 10 REVERB      [V]
    { "Low",        "Body",     "Mid",      "Presence", "Air-Freq", "Volume" },   // 11 CAB-SIM     [V]
    { "Speed",      "Depth",    "-",        "-",        "-",        "-"      },   // 12 TREMOLO     [V]
    { "Speed",      "Depth",    "Mix",      "-",        "-",        "-"      }    // 13 VIBRATO     [ ]
};

uint16_t storedPotValue[NUM_EFFECTS][NUM_FUNC_POTS];
uint16_t storedPreampPotValue[NUM_PREAMPS][NUM_FUNC_POTS];

bool param_selected = true; 
uint8_t selectedEffects[3]; 

int effectListIndex = 0;                 // Hovered item in effect list
int delay_mode_menu_index = 0;           // Selected delay mode in menu
int chorus_mode_menu_index = 0;          // Selected chorus mode in menu
int stereo_mode_menu_index = 0;          // Selected stereo mode in menu
int preamp_select_menu_index = 0;        // Selected preamp in menu

// One cursor per menu so they don't fight other menus
static int delay_fraction_menu_index_l = 0;
static int delay_fraction_menu_index_r = 0;

// Global variable to track the current UI state
UIState currentUI = UI_HOME;
UIState previousUI = UI_HOME;

#define PI 3.14159265

// Global variable for potentiometer values
char pot_labels[NUM_FUNC_POTS][16] = {
    "1", "2", "3", "4", "5", "6"
};