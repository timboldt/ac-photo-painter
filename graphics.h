#ifndef GRAPHICS_H
#define GRAPHICS_H

#include <Arduino.h>
#include <SD.h>
#define GxEPD2_DISPLAY_CLASS GxEPD2_7C
#define GxEPD2_DRIVER_CLASS GxEPD2_730c_ACeP_730
#include <GxEPD2_7C.h>

extern GxEPD2_7C<GxEPD2_730c_ACeP_730, GxEPD2_730c_ACeP_730::HEIGHT> display;

void drawBitmapFromSD_Buffered(const String& filename, int16_t x, int16_t y);
void draw_bmp(const String& filename);

#endif  // GRAPHICS_H