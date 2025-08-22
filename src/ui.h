/* ui.h
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


// UI state enumeration
typedef enum {
    UI_HOME,
    UI_POT,
    UI_VU_IN,
    UI_VU_OUT,
    UI_VU_GAIN,
    UI_EFFECT_LIST,
    UI_DELAY_MODE_MENU,
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
    { "Gain",       "Bass",     "Mid",      "Treble",   "Precense", "Volume" },   // 9  PREAMP      [ ]
    { "Mix",        "Decay",    "Diffuse",  "Dampig",   "Size",     "Volume" },   // 10 REVERB      [V]
    { "Low",        "Body",     "Mid",      "Presence", "Air-Freq", "Volume" },   // 11 CAB-SIM     [V]
    { "Speed",      "Depth",    "-",        "-",        "-",        "-"      },   // 12 TREMOLO     [V]
    { "Speed",      "Depth",    "Mix",      "-",        "-",        "-"      }    // 13 VIBRATO     [ ]
};

uint16_t storedPotValue[NUM_EFFECTS][NUM_FUNC_POTS] = {
    {  600,         2500,          0,       3000,       3000,     2500     },   // 0  CHORUS
    { 2500,          650,          0,        200,          0,     2000     },   // 1  COMPRESSOR
    { 1000,         2000,       2000,       1000,       1200,     2000     },   // 2  DELAY
    { 2000,         3000,       1500,       2000,       2000,     2000     },   // 3  DISTORTION
    { 2000,         2000,       2000,       2000,       4000,     2000     },   // 4  EQ
    { 1000,         1000,       2500,       2000,       3000,     2000     },   // 5  FLANGER
    { 2000,         2000,       2000,       2000,       2000,     2000     },   // 6  FUZZ
    { 2000,         2000,       2000,       2000,       2000,     2000     },   // 7  OVERDRIVE
    {  500,         1250,       3000,       3000,       3000,     2500     },   // 8  PHASER
    { 2000,         2000,       2000,       2000,       2000,     2000     },   // 9  PREAMP
    { 2200,         3600,       POT_MAX,    3000,    POT_MAX,     2000     },   // 10 REVERB
    { 2000,         3000,       1800,       2000,       2500,     2000     },   // 11 CABINET SIMULATION
    { 2000,         2000,          0,          0,          0,        0     },   // 12 TREMOLO
    { 2000,         2000,       2000,          0,          0,        0     }    // 13 VIBRATO
};

int effectListIndex = 0;                 // Hovered item in effect list
int delay_mode_menu_index = 0;           // Selected delay mode in menu
int chorus_mode_menu_index = 0;          // Selected chorus mode in menu
int stereo_mode_menu_index = 0;          // Selected stereo mode in menu
int preamp_select_menu_index = 0;        // Selected preamp in menu
bool param_selected = true;              // Tracks if one of the 3 numbers is selected
uint8_t selectedEffects[3] = {9, 2, 10}; // Default effects

// Global variable to track the current UI state
UIState currentUI = UI_HOME;
UIState previousUI = UI_HOME;

#define PI 3.14159265

// Global variable for potentiometer values
char pot_labels[NUM_FUNC_POTS][16] = {
    "1", "2", "3", "4", "5", "6"
};

// ============================================================================
// === UI helpers ==============================================================
// ============================================================================

static inline void drawMenuTitleBar(const char* title) {
    // Top bar (height 10 works well with 8px font)
    SSD1306_FillRect(0, 0, SCREEN_WIDTH, 10, true);
    SetFont(&Font8x8);
    int titleX = (SCREEN_WIDTH - (int)strlen(title) * 8) / 2;
    // Invert text on filled bar
    SSD1306_DrawString(titleX, 1, title, true);
    // Back to list font for the rest
    SetFont(&Font6x8);
}

// ============================================================================
// === UI - Potentiometers ====================================================
// ============================================================================

void SSD1306_DrawPotentiometer(int x0, int y0, int radius, uint16_t value, uint16_t maxValue, bool color) {
    // Draw the outer circle
    SSD1306_DrawCircle(x0, y0, radius, color);

    // Clamp value to avoid overflow
    if (value > maxValue) value = maxValue;

    // Convert value to angle in radians (0 to 270 degrees, clockwise, starting from -135°)
    float angle = (-225 + (270 * value / maxValue)) * PI / 180;

    // Calculate line end coordinates
    int x1 = x0 + (int)(radius * cosf(angle));
    int y1 = y0 + (int)(radius * sinf(angle));

    // Draw the line
    SSD1306_DrawLine(x0, y0, x1, y1, color);
}

// Draw 6 potentiometers at the bottom of the screen with single-character labels
void SSD1306_DrawPotArray(const char labels[6]) {
    int potCount = 6;
    int radius = 7; // Reasonable size for 128x64 screen
    int spacing = 8; // Space between pots
    int totalWidth = potCount * (radius * 2) + (potCount - 1) * spacing;
    int startX = (SCREEN_WIDTH - totalWidth) / 2 + radius;
    int y0 = SCREEN_HEIGHT - radius - 14; // Leave space for label

    for (int i = 0; i < potCount; i++) {
        int x = startX + i * (radius * 2 + spacing);

        // Draw potentiometer
        SSD1306_DrawPotentiometer(x, y0, radius, storedPotValue[selectedEffects[selected_slot]][i], POT_MAX, true);

        // Draw label below
        SetFont(&Font6x8);
        int labelX = x - 3; // Center label under pot
        int labelY = y0 + radius + 4;
        SSD1306_DrawChar(labelX, labelY, labels[i], false);
    }
}

// ============================================================================
// === UI - VU Meters == ======================================================
// ============================================================================

void drawVUMeter(int x, int y, int w, int h, uint32_t value, const char* label) {

    // Set safe angle for the needle and tick amrks
    float maxAngle = 40;
    int totalMarks = 10;

    // Draw outer rectangle
    SSD1306_DrawRect(x, y, w, h, true);

    // Pivot point at bottom-center
    int cx = x + w / 2;
    int cy = y + h - 2;

    // Safe radii
    int needleLen = h - 7;            // 1px margin inside the box
    int tickOuter = needleLen;
    int tickInner = tickOuter - 4;


    float angleRange = maxAngle * 2.0f;
    float angleStep = angleRange / (totalMarks - 1);

    for (int i = 0; i < totalMarks; i++) {
        float angleDeg = -maxAngle + i * angleStep;
        float angleRad = (angleDeg - 90.0f) * PI / 180.0f;

        // Draw regular evenly spaced tick marks
        int inner = tickInner;
        int outer = tickOuter;

        int x1 = cx + cosf(angleRad) * inner;
        int y1 = cy + sinf(angleRad) * inner;
        int x2 = cx + cosf(angleRad) * outer;
        int y2 = cy + sinf(angleRad) * outer;

        SSD1306_DrawLine(x1, y1, x2, y2, true);
    }


    // Map value (0..2147483392) to angle -50° to +50°
    float needleAngleDeg = -maxAngle + (value * (maxAngle*2) / 2147483392.0f);
    float needleRad = (needleAngleDeg - 90) * PI / 180;

    int nx = cx + cosf(needleRad) * needleLen;
    int ny = cy + sinf(needleRad) * needleLen;

    SSD1306_DrawLine(cx, cy, nx, ny, true);

    // Label
    int labelX = x + (w - 6) / 2;
    int labelY = y + h + 3;
    SetFont(&Font6x8);
    SSD1306_DrawChar(labelX, labelY, label[0], false);
}

// Draw both meters and the main label
void drawStereoVUMeters(uint32_t leftValue, uint32_t rightValue, const char* labelText, bool smooth) {
    static uint32_t displayedLeftValue = 0;
    static uint32_t displayedRightValue = 0;

    // Default natural smoothing
    const uint32_t deadzone = 50000;     // ignore small value changes to reduce jitter
    uint32_t decayStep = 60000000;  // decay speed

    // fast smoothing
    if (!smooth) {
        decayStep = 150000000;  // decay speed
    }

    // Left channel smoothing
    if (leftValue > displayedLeftValue) {
        displayedLeftValue = leftValue;  // instant rise
    } else if (displayedLeftValue > leftValue + deadzone) {
        uint32_t diff = displayedLeftValue - leftValue;
        if (diff > decayStep)
            displayedLeftValue -= decayStep;
        else
            displayedLeftValue = leftValue;
    }

    // Right channel smoothing
    if (rightValue > displayedRightValue) {
        displayedRightValue = rightValue;  // instant rise
    } else if (displayedRightValue > rightValue + deadzone) {
        uint32_t diff = displayedRightValue - rightValue;
        if (diff > decayStep)
            displayedRightValue -= decayStep;
        else
            displayedRightValue = rightValue;
    }

    // Clear screen and draw meters with smoothed values
    SSD1306_FillRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, false);

    int meterWidth = 52;
    int meterHeight = 42;
    int spacing = 4;

    int leftX = 10;
    int rightX = leftX + meterWidth + spacing;
    int meterY = 4;

    drawVUMeter(leftX, meterY, meterWidth, meterHeight, displayedLeftValue, "L");
    drawVUMeter(rightX, meterY, meterWidth, meterHeight, displayedRightValue, "R");

    SetFont(&Font6x8);
    int labelX = (SCREEN_WIDTH - strlen(labelText) * 6) / 2;
    int labelY = SCREEN_HEIGHT - 8;
    SSD1306_DrawString(labelX, labelY, labelText, false);
}

// ============================================================================
// === UI - HOME screen =======================================================
// ============================================================================

// Draw the home screen with centered effect name and parameter slots
void drawHomeScreen(uint16_t hoveredIndex, bool effectJustChanged, uint8_t currentEffectSlot) {
    const char* effectName = allEffects[selectedEffects[currentEffectSlot]];
    // Overwrite with preamp name when selected
    if(selectedEffects[currentEffectSlot] == PREAMP_EFFECT_INDEX){
        effectName = preamp_names[selected_preamp_style];
    }

    char buf[16];
    SetFont(&Font8x8);
    snprintf(buf, sizeof(buf), effectName);
    int labelX = (128 - strlen(buf) * 8) / 2;

    // --- Position 0: LEFT ARROW ---
    if (hoveredIndex == 0) {
        SSD1306_DrawTriangle(0, 20, 6, 14, 6, 26, 1);
    }

    // --- Position 1: EFFECT NAME ---
    bool hoveringEffectName = (hoveredIndex == 1);
    bool effectChangedRecently = (effectJustChanged && hoveredIndex == 1);

    if (effectChangedRecently) {
        SSD1306_FillRect(labelX - 2, 0, strlen(buf) * 8 + 4, 9, 1);
        SSD1306_DrawString(labelX, 1, buf, true);
    } else if (hoveringEffectName) {
        SSD1306_DrawRect(labelX - 2, 0, strlen(buf) * 8 + 4, 9, 1);
        SSD1306_DrawString(labelX, 1, buf, false);
    } else {
        SSD1306_DrawString(labelX, 1, buf, false);
    }

    // --- Positions 2–4: Effect slot selection ---
    SetFont(&Font6x8);
    const int numSlots = 3;
    const int itemWidth = 9;
    const int spacing = 4;
    int totalWidth = numSlots * itemWidth + (numSlots - 1) * spacing;
    int startX = (128 - totalWidth) / 2;

    for (int i = 0; i < numSlots; i++) {
        int x = startX + i * (itemWidth + spacing);
        snprintf(buf, sizeof(buf), "%d", i + 1);

        bool isHovered = ((i + 2) == hoveredIndex);
        bool isActiveSlot = (effectJustChanged && currentEffectSlot == i);

        if (isActiveSlot) {
            SSD1306_FillRect(x + 1, 15, itemWidth, 9, 1);
            SSD1306_DrawString(x + 2, 16, buf, true);
        } else {
            SSD1306_DrawString(x + 2, 16, buf, false);
        }

        if (isHovered) {
            SSD1306_DrawRect(x, 14, itemWidth + 2, 11, 1);
        }
    }

    // --- Position 5: RIGHT ARROW ---
    if (hoveredIndex == 5) {
        SSD1306_DrawTriangle(127, 20, 121, 14, 121, 26, 1);
    }

    // --- Pot labels based on current effect ---
    uint8_t selectedEffectID = selectedEffects[currentEffectSlot];
    for (int i = 0; i < NUM_FUNC_POTS; ++i) {
        strncpy(pot_labels[i], potLabelSets[selectedEffectID][i], sizeof(pot_labels[i]) - 1);
        pot_labels[i][sizeof(pot_labels[i]) - 1] = '\0';  // Ensure null-termination
    }

    char short_labels[NUM_FUNC_POTS];
    for (int i = 0; i < NUM_FUNC_POTS; ++i) {
        short_labels[i] = pot_labels[i][0];  // Use first char as short label
    }
    SSD1306_DrawPotArray(short_labels);

    // Draw cpu usage in top left corner
    if(SHOW_CPU){
        char cpuUsageStr[6];
        SetFont(&Font6x8);
        snprintf(cpuUsageStr, sizeof(cpuUsageStr), "%d%%", (int)cpu0_peak_usage);
        SSD1306_DrawString(0, 0, cpuUsageStr, false);
    }
}

// ============================================================================
// === UI - Effects List ======================================================
// ============================================================================

// Draw the effect list screen with scrolling
void drawEffectListScreen(int selectedIndex) {
    SetFont(&Font6x8);

    const int visibleRows = 6;
    int startIdx = selectedIndex - visibleRows / 2;
    if (startIdx < 0) startIdx = 0;
    if (startIdx > NUM_EFFECTS - visibleRows) startIdx = NUM_EFFECTS - visibleRows;

    
    for (int i = 0; i < visibleRows; ++i) {
        int effectIdx = startIdx + i;
        if (effectIdx >= NUM_EFFECTS) break;

        const char* name = allEffects[effectIdx];
        
        // Check if the effect name is not already selected in another selected_slot
        for (int j = 0; j < 3; ++j) {
            if (selectedEffects[j] == effectIdx) {
                // Append the name with a marker

                char markedName[20];  // Make sure this is long enough
                int nameLen = strlen(name);
                int padLen = 13 - nameLen;   // Offset by some characters
                if (padLen < 0) padLen = 0;  // Prevent negative padding

                snprintf(markedName, sizeof(markedName), "%s%*s [%d]", name, padLen, "", j + 1);

                name = markedName;  // Use the marked name
                break;
            }
        }

        int y = i * 10;

        bool isHovered = (effectIdx == selectedIndex);
        if (isHovered) {
            SSD1306_FillRect(0, y, 128, 10, 1);
            SSD1306_DrawString(2, y + 1, name, true);
        } else {
            SSD1306_DrawString(2, y + 1, name, false);
        }
    }
}

// ============================================================================
// === UI - Delay Mode Screen =================================================
// ============================================================================

void drawDelayModeMenu(int selectedIndex) {
    SSD1306_FillRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, false);

    // Title: current effect name
    drawMenuTitleBar(allEffects[effectListIndex]);

    // List starts below the title bar
    const int rowH = 10;
    const int startY = 12;

    for (int i = 0; i < NUM_DELAY_MODES; ++i) {
        int y = startY + i * rowH;
        const char* name = delay_mode_names[i];

        if (i == selectedIndex) {
            SSD1306_FillRect(0, y, 128, rowH, 1);
            SSD1306_DrawString(2, y + 1, name, true);
            selected_delay_mode = (DelayMode)i;  // live update
        } else {
            SSD1306_DrawString(2, y + 1, name, false);
        }
    }
}

// ============================================================================
// === UI - Chorus Mode Screen ================================================
// ============================================================================

// no need to include chorus.h
extern volatile int8_t ui_chorus_mode_pending;

void drawChorusModeMenu(int selectedIndex) {
    static int last_selected = -1;

    if (last_selected == -1) {
        selectedIndex = selected_chorus_mode; // restore cursor on first draw
        last_selected = selectedIndex;
    }

    SSD1306_FillRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, false);

    drawMenuTitleBar(allEffects[effectListIndex]);

    const int rowH = 10;
    const int startY = 12;

    for (int i = 0; i < NUM_CHORUS_MODES; ++i) {
        int y = startY + i * rowH;
        const char* name = chorus_mode_names[i];

        if (i == selectedIndex) {
            if (selectedIndex != last_selected) {
                selected_chorus_mode = selectedIndex;          // persist
                ui_chorus_mode_pending = (int8_t)selectedIndex; // signal DSP
                last_selected = selectedIndex;
            }
            SSD1306_FillRect(0, y, 128, rowH, 1);
            SSD1306_DrawString(2, y + 1, name, true);
        } else {
            SSD1306_DrawString(2, y + 1, name, false);
        }
    }
}

// ============================================================================
// === UI - Stereo Mode Screen ================================================
// ============================================================================

void drawStereoModeMenu(int selectedIndex) {
    SSD1306_FillRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, false);

    drawMenuTitleBar(allEffects[effectListIndex]);

    const int rowH = 10;
    const int startY = 12;

    for (int i = 0; i < NUM_STEREO_MODES; ++i) {
        int y = startY + i * rowH;
        const char* name = stereo_mode_names[i];

        if (i == selectedIndex) {
            SSD1306_FillRect(0, y, 128, rowH, 1);
            SSD1306_DrawString(2, y + 1, name, true);

            // live update which effect’s stereo mode is being edited
            if (effectListIndex == FLNG_EFFECT_INDEX) {
                selected_flanger_mode = (FXmode)i;
            } else if (effectListIndex == PHSR_EFFECT_INDEX) {
                selected_phaser_mode = (FXmode)i;
            } else if (effectListIndex == TREM_EFFECT_INDEX) {
                selected_tremolo_mode = (FXmode)i;
            } else if (effectListIndex == VIBR_EFFECT_INDEX) {
                selected_vibrato_mode = (FXmode)i;
            }
        } else {
            SSD1306_DrawString(2, y + 1, name, false);
        }
    }
}

// ============================================================================
// === UI - Preamp selection screen ===========================================
// ============================================================================

void drawPreampSelectMenu(int selectedIndex) {  // [NEW]
    static int last_selected = -1;

    if (last_selected == -1) {
        selectedIndex = selected_preamp_style; // restore cursor on first draw
        last_selected = selectedIndex;
    }

    SSD1306_FillRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, false);

    drawMenuTitleBar(allEffects[effectListIndex]);

    const int rowH = 10;
    const int startY = 12;

    for (int i = 0; i < NUM_PREAMPS; ++i) {
        int y = startY + i * rowH;
        const char* name = preamp_names[i];

        if (i == selectedIndex) {
            selected_preamp_style = (preamp)i;
            SSD1306_FillRect(0, y, 128, rowH, 1);
            SSD1306_DrawString(2, y + 1, name, true);
        } else {
            SSD1306_DrawString(2, y + 1, name, false);
        }
    }
}

// ============================================================================
// === UI - POTS screen =======================================================
// ============================================================================

// Draw potentiometer control screen
void drawPotScreen(uint8_t pot_index, uint16_t selected) {
    SSD1306_DrawPotentiometer(64, 25, 22, pot_value[pot_index], 4095, 1);
    SetFont(&Font8x8);

    // Variable for the label
    const char* label = "";

    // Check if it is one of the function pots
    if (pot_index < NUM_FUNC_POTS){
        label = pot_labels[pot_index];
    }
    // One pot up is always the volume pot
    else if (pot_index == NUM_FUNC_POTS){
        label = "Volume";
    }
    // Last pot is always the EXP2
    else{
        label = "EXP-2";
    }

    int labelX = (128 - strlen(label) * 8) / 2;
    SSD1306_DrawString(labelX, 56, label, false);

    if (selected == 0)
        SSD1306_DrawTriangle(0, 32, 6, 26, 6, 38, 1);
    else if (selected == 1)
        SSD1306_DrawTriangle(127, 32, 121, 26, 121, 38, 1);
}

// Draw stereo VU meter screen
void drawVUMeterScreen(int valueLeft, int valueRight, uint16_t selected, uint8_t input) {
    // Draw the VU meter
    if (input == 0) {
        drawStereoVUMeters(valueLeft, valueRight, "INPUT", true);
    }
    else if (input == 1) {
        drawStereoVUMeters(valueLeft, valueRight, "OUTPUT", true);
    }
    else{
        drawStereoVUMeters(valueLeft, valueRight, "GAIN", false);
    }
    if (selected == 0) SSD1306_DrawTriangle(0, 32, 6, 26, 6, 38, 1);
    else if (selected == 1) SSD1306_DrawTriangle(127, 32, 121, 26, 121, 38, 1);
}