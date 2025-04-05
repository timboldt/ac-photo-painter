#ifndef GRAPHICS_H
#define GRAPHICS_H

#include <Arduino.h>
#include <SD.h>
#define GxEPD2_DISPLAY_CLASS GxEPD2_7C
#define GxEPD2_DRIVER_CLASS GxEPD2_730c_ACeP_730
#include <GxEPD2_7C.h>

#define MAX_DISPLAY_BUFFER_SIZE 5000
#define MAX_HEIGHT(EPD) \
    (EPD::HEIGHT <= (MAX_DISPLAY_BUFFER_SIZE) / (EPD::WIDTH / 2) ? EPD::HEIGHT : (MAX_DISPLAY_BUFFER_SIZE) / (EPD::WIDTH / 2))

extern GxEPD2_DISPLAY_CLASS<GxEPD2_DRIVER_CLASS, MAX_HEIGHT(GxEPD2_DRIVER_CLASS)> display;

void drawBitmapFromSD_Buffered(const char* filename, int16_t x, int16_t y, bool with_color, bool partial_update, bool overwrite);
void draw_bmp(const char* filename);

#endif // GRAPHICS_H