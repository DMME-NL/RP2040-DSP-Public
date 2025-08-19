/* ssd1306.h
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

#ifndef SSD1306_H
#define SSD1306_H

#include <stdint.h>
#include <stdbool.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define SSD1306_BUFFER_SIZE (SCREEN_WIDTH * SCREEN_HEIGHT / 8)

void SSD1306_Init(void);
void SSD1306_ClearScreen(void);
void SSD1306_UpdateScreen(void);


// Pixel and Drawing Primitives
void SSD1306_DrawPixel(int x, int y, bool color);
void SSD1306_DrawLine(int x0, int y0, int x1, int y1, bool color);

// Characters and text
void SSD1306_DrawChar(int x, int y, char c, bool inverted);
void SSD1306_DrawString(int x, int y, const char *str, bool inverted);

// Rectangles
void SSD1306_DrawRect(int x, int y, int w, int h, bool color);
void SSD1306_FillRect(int x, int y, int w, int h, bool color);

// Circles
void SSD1306_DrawCircle(int x0, int y0, int radius, bool color);
void SSD1306_FillCircle(int x0, int y0, int radius, bool color);

// Triangles
void SSD1306_DrawTriangle(int x0, int y0, int x1, int y1, int x2, int y2, bool color);
void SSD1306_FillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, bool color);

extern uint8_t screen_buffer[SSD1306_BUFFER_SIZE];

void oled_draw_bitmap(int x, int y, int w, int h, const uint8_t *bitmap, bool inverted);
void SSD1306_DrawSplashLogoBitmap(int x, int y, bool inverted);

#endif
