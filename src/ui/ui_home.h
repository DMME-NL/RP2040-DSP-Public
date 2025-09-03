/* ui_home.h
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
// === UI - HOME screen =======================================================
// ============================================================================

typedef enum {
    HI_LEFT_ARROW = 0,
    HI_EFFECT_NAME,
    HI_LEFT_TAP,    // only when tap_l_visible()
    HI_SLOT_1,
    HI_SLOT_2,
    HI_SLOT_3,
    HI_RIGHT_TAP,   // only when tap_r_visible()
    HI_RIGHT_ARROW,
} HomeItemType;

typedef struct {
    HomeItemType type;
    uint8_t      slot_index; // for slots (0..2), unused otherwise
} HomeItem;

#define HI_MAX 8
static HomeItem home_items[HI_MAX];
static int home_items_count = 0;

// Build dynamic sequence based on current context.
// Order requirement: LEFT_TAP before slots, RIGHT_TAP after slots.
static void buildHomeItems(uint8_t currentEffectSlot) {
    home_items_count = 0;
    home_items[home_items_count++] = (HomeItem){ HI_LEFT_ARROW,  0 };
    home_items[home_items_count++] = (HomeItem){ HI_EFFECT_NAME, 0 };

    if (tap_l_visible(currentEffectSlot))
        home_items[home_items_count++] = (HomeItem){ HI_LEFT_TAP, 0 };

    home_items[home_items_count++] = (HomeItem){ HI_SLOT_1, 0 };
    home_items[home_items_count++] = (HomeItem){ HI_SLOT_2, 1 };
    home_items[home_items_count++] = (HomeItem){ HI_SLOT_3, 2 };

    if (tap_r_visible(currentEffectSlot))
        home_items[home_items_count++] = (HomeItem){ HI_RIGHT_TAP, 0 };

    home_items[home_items_count++] = (HomeItem){ HI_RIGHT_ARROW, 0 };
}

// Given a hovered index, clamps & ensures skip is automatic because the
// invisible items simply aren't in the list.
static int clampHomeIndex(int idx) {
    if (idx < 0) return 0;
    if (idx >= home_items_count) return home_items_count - 1;
    return idx;
}

void drawHomeScreen(uint16_t hoveredIndex, bool effectJustChanged, uint8_t currentEffectSlot) {
    buildHomeItems(currentEffectSlot);                       // NEW
    hoveredIndex = clampHomeIndex((int)hoveredIndex);        // NEW

    const char* effectName = allEffects[selectedEffects[currentEffectSlot]];
    if (selectedEffects[currentEffectSlot] == PREAMP_EFFECT_INDEX) {
        effectName = preamp_names[selected_preamp_style];
    }

    char buf[16];
    SetFont(&Font8x8);
    snprintf(buf, sizeof(buf), "%s", effectName); 
    int labelX = (128 - (int)strlen(buf) * 8) / 2;

    // Clear background area for safety
    // (not a full clear; your existing code handles other fills)
    // --- LEFT/RIGHT ARROWS & EFFECT NAME ------------------------------------
    // We'll draw them based on hover below.

    // Draw EFFECT NAME with hover/changed styles
    bool hoveringEffectName = (home_items[hoveredIndex].type == HI_EFFECT_NAME);
    bool effectChangedRecently = (effectJustChanged && hoveringEffectName);
    if (effectChangedRecently) {
        SSD1306_FillRect(labelX - 2, 0, (int)strlen(buf) * 8, 9, 1);
        //SSD1306_DrawString(labelX, 1, buf, true);
        drawMenuTitleBar(effectName);
    } else if (hoveringEffectName) {
        SSD1306_DrawRect(labelX - 2, 0, (int)strlen(buf) * 8, 9, 1);
        SSD1306_DrawString(labelX, 1, buf, false);
    } else {
        SSD1306_DrawString(labelX, 1, buf, false);
    }

    // Left arrow
    int arrowYoffset = -14;
    if (home_items[hoveredIndex].type == HI_LEFT_ARROW) {
        SSD1306_DrawTriangle(0, 20+arrowYoffset, 6, 14+arrowYoffset, 6, 26+arrowYoffset, 1);
    }

    // Right arrow
    if (home_items[hoveredIndex].type == HI_RIGHT_ARROW) {
        SSD1306_DrawTriangle(127, 20+arrowYoffset, 121, 14+arrowYoffset, 121, 26+arrowYoffset, 1);
    }

    // --- LEFT TAP (if visible) ---------------------------------------------- NEW
    SetFont(&Font6x8);
    if (tap_l_visible(currentEffectSlot)) {
        char leftTapStr[8];
        snprintf(leftTapStr, sizeof(leftTapStr), "%s", delay_fraction_name[delay_time_fraction_l]);
        int x = 2, y = 22;
        if (home_items[hoveredIndex].type == HI_LEFT_TAP) {
            // draw a hover box behind the text
            int w = ((int)strlen(leftTapStr)) * 6 + 4;
            SSD1306_FillRect(x - 1, y - 1, w, 9, 1);
            SSD1306_DrawString(x, y, leftTapStr, true);
        } else {
            SSD1306_DrawString(x, y, leftTapStr, false);
        }
    } else if (delay_is_selected(currentEffectSlot) && !tap_tempo_active_l) {
        // Show the numeric delay time on the left when tap is disabled (as before)
        char delayStr[8];
        float lDelay = delay_samples_l * 1000.0f / SAMPLE_RATE;
        if (lDelay > 997.0f) {
            lDelay = lDelay / 1000.0f;
            snprintf(delayStr, sizeof(delayStr), "%.2fs", lDelay);
        } else {
            int ms = (int)(lDelay + 0.5f);
            ms = (ms + 2) / 5 * 5;   // round to nearest 5
            snprintf(delayStr, sizeof(delayStr), "%dms", ms);
        }
        SSD1306_DrawString(2, 22, delayStr, false);
    }

    // --- RIGHT TAP (if visible) --------------------------------------------- NEW
    SetFont(&Font6x8);
    if (tap_r_visible(currentEffectSlot)) {
        char rightTapStr[8];
        snprintf(rightTapStr, sizeof(rightTapStr), "%s", delay_fraction_name[delay_time_fraction_r]);
        int len = (int)strlen(rightTapStr);
        int x = 128 - (len + 1) * 6 - 2;
        int y = 22;

        if (home_items[hoveredIndex].type == HI_RIGHT_TAP) {
            int w = len * 6 + 4;
            SSD1306_FillRect(x - 1, y - 1, w, 9, 1);
            SSD1306_DrawString(x, y, rightTapStr, true);
        } else {
            SSD1306_DrawString(x, y, rightTapStr, false);
        }
    } else if (delay_is_selected(currentEffectSlot) && !tap_tempo_active_r) {
        // Show the numeric delay time on the right when tap is disabled (as before)
        char delayStr[8];
        float rDelay = delay_samples_r * 1000.0f / SAMPLE_RATE;
        if (rDelay > 997.0f) {
            rDelay = rDelay / 1000.0f;
            snprintf(delayStr, sizeof(delayStr), "%.2fs", rDelay);
        } else {
            int ms = (int)(rDelay + 0.5f);
            ms = (ms + 2) / 5 * 5;   // round to nearest 5
            snprintf(delayStr, sizeof(delayStr), "%dms", ms);
        }
        int len = (int)strlen(delayStr);
        SSD1306_DrawString(128 - (len + 1) * 6 - 2, 22, delayStr, false);
    }

    // --- 3 SLOT BUTTONS ------------------------------------------------------
    SetFont(&Font6x8);
    const int numSlots = 3;
    const int itemWidth = 9;
    const int spacing = 4;
    int totalWidth = numSlots * itemWidth + (numSlots - 1) * spacing;
    int startX = (128 - totalWidth) / 2;

    for (int i = 0; i < numSlots; i++) {
        int x = startX + i * (itemWidth + spacing);
        snprintf(buf, sizeof(buf), "%d", i + 1);

        bool isActiveSlot = (effectJustChanged && currentEffectSlot == i);

        // Is this slot hovered?
        bool isHovered = false;
        if (home_items[hoveredIndex].type == HI_SLOT_1 && i == 0) isHovered = true;
        if (home_items[hoveredIndex].type == HI_SLOT_2 && i == 1) isHovered = true;
        if (home_items[hoveredIndex].type == HI_SLOT_3 && i == 2) isHovered = true;

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

    // --- Pot labels based on current effect (unchanged) ----------------------
    uint8_t selectedEffectID = selectedEffects[currentEffectSlot];
    for (int i = 0; i < NUM_FUNC_POTS; ++i) {
        strncpy(pot_labels[i], potLabelSets[selectedEffectID][i], sizeof(pot_labels[i]) - 1);
        pot_labels[i][sizeof(pot_labels[i]) - 1] = '\0';
    }

    char short_labels[NUM_FUNC_POTS];
    for (int i = 0; i < NUM_FUNC_POTS; ++i) {
        short_labels[i] = pot_labels[i][0];
    }
    SSD1306_DrawPotArray(short_labels);

    if (SHOW_CPU) {
        char cpuUsageStr[6];
        SetFont(&Font6x8);
        snprintf(cpuUsageStr, sizeof(cpuUsageStr), "%d%%", (int)cpu0_peak_usage);
        // count characters to right-align
        int len = (int)strlen(cpuUsageStr);
        int cpuX = 128 - ((len+1) * 6) - 1;
        int cpuY = 32 - 14;
        bool invert = false;
        if(selectedEffects[currentEffectSlot] == DELAY_EFFECT_INDEX) {
            // Set different position for delay effect
            cpuY = 1;
            if(home_items[hoveredIndex].type == HI_EFFECT_NAME) {
                invert = true;
            }
        }
        SSD1306_DrawString(cpuX, cpuY, cpuUsageStr, invert);
    }
}