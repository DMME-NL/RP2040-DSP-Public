/* font.c
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

#include "font.h"
#include "font5x8.h"
#include "font6x8.h"
#include "font8x8.h"

const FontDef Font5x8 = { font5x8, 5, 8, 0x20, 0x7E };
const FontDef Font6x8 = { font6x8, 6, 8, 0x20, 0x7E };
const FontDef Font8x8 = { font8x8, 8, 8, 0x20, 0x7E };

const FontDef *ActiveFont = &Font5x8;

void SetFont(const FontDef *font) {
    ActiveFont = font;
}
