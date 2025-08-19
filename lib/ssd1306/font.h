/* fonr.h
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

#ifndef FONT_H
#define FONT_H

#include <stdint.h>

typedef struct {
    const uint8_t *data;
    uint8_t width;
    uint8_t height;
    uint8_t first_char;
    uint8_t last_char;
} FontDef;

extern const FontDef Font5x8;
extern const FontDef Font6x8;
extern const FontDef Font8x8;

extern const FontDef *ActiveFont;
void SetFont(const FontDef *font);

#endif
