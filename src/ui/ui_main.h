/* ui_main.h
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

#include "ui_variables.h"


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

static inline bool delay_is_selected(uint8_t slot) {
    return selectedEffects[slot] == DELAY_EFFECT_INDEX;
}
static inline bool tap_l_visible(uint8_t slot) {
    return delay_is_selected(slot) && tap_tempo_active_l;
}
static inline bool tap_r_visible(uint8_t slot) {
    return delay_is_selected(slot) && tap_tempo_active_r;
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

        // check if we are in preamp mode
        if (selectedEffects[selected_slot] == PREAMP_EFFECT_INDEX) 
            SSD1306_DrawPotentiometer(x, y0, radius, storedPreampPotValue[selected_preamp_style][i], POT_MAX, true);
        else
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
// === UI - Home Screen =======================================================
// ============================================================================

#include "ui_home.h"

// ============================================================================
// === UI - Effects List ======================================================
// ============================================================================

// Draw the effect list screen with scrolling
void drawEffectListScreen(int selectedIndex) {
    SetFont(&Font6x8);

    // --- LIVE UPDATE: assign hovered effect to active slot if it's unique ---
    if (selectedIndex >= 0 && selectedIndex < NUM_EFFECTS) {
        bool taken = false;
        for (int j = 0; j < 3; ++j) {
            if (j != selected_slot && selectedEffects[j] == selectedIndex) {
                taken = true;
                break;
            }
        }
        if (!taken) {
            // selectedIndex is the hovered effect — use it as the effectListIndex
            selectedEffects[selected_slot] = selectedIndex;
        }
    }
    // -----------------------------------------------------------------------

    const int visibleRows = 6;
    int startIdx = selectedIndex - visibleRows / 2;
    if (startIdx < 0) startIdx = 0;
    if (startIdx > NUM_EFFECTS - visibleRows) startIdx = NUM_EFFECTS - visibleRows;
    if (startIdx < 0) startIdx = 0; // handle NUM_EFFECTS < visibleRows

    for (int i = 0; i < visibleRows; ++i) {
        int effectIdx = startIdx + i;
        if (effectIdx >= NUM_EFFECTS) break;

        const char* name = allEffects[effectIdx];

        // Mark rows that are already selected in any slot
        int inSlot = -1;
        for (int j = 0; j < 3; ++j) {
            if (selectedEffects[j] == effectIdx) { inSlot = j; break; }
        }
        if (inSlot >= 0) {
            // Enough room for name + padding + " [n]"
            char markedName[24];
            int nameLen = (int)strlen(name);
            int padLen  = 13 - nameLen;
            if (padLen < 0) padLen = 0;
            snprintf(markedName, sizeof(markedName), "%s%*s [%d]", name, padLen, "", inSlot + 1);
            name = markedName;
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
// === UI - Delay Fraction Screen =============================================
// ============================================================================

static inline int clamp_fraction_index(int idx) {
    if (idx < 0) return 0;
    if (idx >= (int)NUM_FRACTIONS) return (int)NUM_FRACTIONS - 1;
    return idx;
}

int lastHoveredIndex = 0;

static void drawDelayFractionMenuCommon(bool left, int hoveredIndex) {
    SSD1306_FillRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, false);
    drawMenuTitleBar(left ? "LEFT TAP" : "RIGHT TAP");

    const int rowH        = 10;
    const int startY      = 12;
    const int visibleRows = 5;
    const int total       = (int)NUM_FRACTIONS;

    // Clamp hovered index
    if (hoveredIndex < 0) hoveredIndex = 0;
    if (hoveredIndex >= total) hoveredIndex = total - 1;

    // === Always update the selected fraction in real time ===
    if (left) delay_time_fraction_l = (DelayFraction)hoveredIndex;
    else      delay_time_fraction_r = (DelayFraction)hoveredIndex;

    // Signal update if hovered index changed
    if (hoveredIndex != lastHoveredIndex) {
        // if(DEBUG){printf("Update Delay flag: SET");}
        updateDelayFlag = true;
        lastHoveredIndex = hoveredIndex;
    }

    const DelayFraction current_sel = left ? delay_time_fraction_l : delay_time_fraction_r;

    // Compute window so hovered item stays centered when possible
    int startIdx = hoveredIndex - visibleRows / 2;
    if (startIdx < 0) startIdx = 0;
    if (startIdx > total - visibleRows) startIdx = total - visibleRows;
    if (startIdx < 0) startIdx = 0;

    // Draw visible window
    for (int row = 0; row < visibleRows; ++row) {
        int i = startIdx + row;
        if (i >= total) break;

        int y = startY + row * rowH;
        const char* name = delay_fraction_name[i];

        char line[12];
        snprintf(line, sizeof(line), "%s %s", (i == current_sel ? "\xFB" : " "), name);

        if (i == hoveredIndex) {
            SSD1306_FillRect(0, y, 128, rowH, 1);
            SSD1306_DrawString(2, y + 1, line, true);
        } else {
            SSD1306_DrawString(2, y + 1, line, false);
        }
    }
}

void drawDelayFractionMenuL(int hoveredIndex) { drawDelayFractionMenuCommon(true,  hoveredIndex); }
void drawDelayFractionMenuR(int hoveredIndex) { drawDelayFractionMenuCommon(false, hoveredIndex); }


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

    // if delay is selected and pot 0 or 1, show the delay time in ms
    if (selectedEffects[selected_slot] == DELAY_EFFECT_INDEX) {
        char delayStr[16];
        if (pot_index == 0) {
            if(!tap_tempo_active_l){
                // Draw the left delay time in ms or seconds if >1000ms
                float lDelay = delay_samples_l * 1000 / SAMPLE_RATE;
                if (lDelay > 997) {
                    // Convert to seconds and round to 0.01s
                    lDelay = lDelay / 1000;
                    snprintf(delayStr, sizeof(delayStr), "%.2f", lDelay);
                } 
                else {
                    int ms = (int)(lDelay + 0.5f);
                    ms = (ms + 2) / 5 * 5;   // round to nearest 5
                    snprintf(delayStr, sizeof(delayStr), "%dm", (int)ms);
                }
                int len = (int)strlen(delayStr);
                SSD1306_DrawString(0, 0, delayStr, false);
            }
        }
        else if (pot_index == 1) {
            if(!tap_tempo_active_l){
                // Draw the right delay time in ms or seconds if >1000ms
                float rDelay = delay_samples_r * 1000 / SAMPLE_RATE;
                if (rDelay > 997) {
                    // Convert to seconds and round to 0.01s
                    rDelay = rDelay / 1000;
                    snprintf(delayStr, sizeof(delayStr), "%.2f", rDelay);
                } 
                else {
                    int ms = (int)(rDelay + 0.5f);
                    ms = (ms + 2) / 5 * 5;   // round to nearest 5                    
                    snprintf(delayStr, sizeof(delayStr), "%dm", (int)ms);
                }
                int len = (int)strlen(delayStr);
                SSD1306_DrawString(128 - (len+1) * 8, 0, delayStr, false);
            }
        }
    }

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