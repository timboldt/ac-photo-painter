#include "graphics.h"

static const uint16_t input_buffer_pixels = 800;
static const uint16_t max_row_width = 1448;
static const uint16_t max_palette_pixels = 256;

uint8_t input_buffer[3 * input_buffer_pixels];
uint8_t output_row_mono_buffer[max_row_width / 8];
uint8_t output_row_color_buffer[max_row_width / 8];
uint8_t mono_palette_buffer[max_palette_pixels / 8];
uint8_t color_palette_buffer[max_palette_pixels / 8];
uint16_t rgb_palette_buffer[max_palette_pixels];

uint16_t read16(File& f) {
    uint16_t result;
    ((uint8_t*)&result)[0] = f.read();
    ((uint8_t*)&result)[1] = f.read();
    return result;
}

uint32_t read32(File& f) {
    uint32_t result;
    ((uint8_t*)&result)[0] = f.read();
    ((uint8_t*)&result)[1] = f.read();
    ((uint8_t*)&result)[2] = f.read();
    ((uint8_t*)&result)[3] = f.read();
    return result;
}

void drawBitmapFromSD_Buffered(const char* filename, int16_t x, int16_t y, bool with_color, bool partial_update, bool overwrite) {
    File file;
    bool valid = false;
    bool flip = true;
    bool has_multicolors = (display.epd2.panel == GxEPD2::ACeP565) || (display.epd2.panel == GxEPD2::GDEY073D46);
    uint32_t startTime = millis();

    if ((x >= display.width()) || (y >= display.height())) return;

    Serial.println();
    Serial.print("Loading image '");
    Serial.print(filename);
    Serial.println('"');

    file = SD.open(filename);
    if (!file) {
        Serial.print("File not found");
        return;
    }

    if (read16(file) == 0x4D42) {
        uint32_t fileSize = read32(file);
        uint32_t creatorBytes = read32(file);
        uint32_t imageOffset = read32(file);
        uint32_t headerSize = read32(file);
        uint32_t width = read32(file);
        int32_t height = (int32_t)read32(file);
        uint16_t planes = read16(file);
        uint16_t depth = read16(file);
        uint32_t format = read32(file);

        if ((planes == 1) && ((format == 0) || (format == 3))) {
            uint32_t rowSize = (width * depth / 8 + 3) & ~3;
            if (depth < 8) rowSize = ((width * depth + 8 - depth) / 8 + 3) & ~3;
            if (height < 0) {
                height = -height;
                flip = false;
            }

            uint16_t w = width;
            uint16_t h = height;
            if ((x + w - 1) >= display.width()) w = display.width() - x;
            if ((y + h - 1) >= display.height()) h = display.height() - y;

            valid = true;

            if (partial_update)
                display.setPartialWindow(x, y, w, h);
            else
                display.setFullWindow();

            display.firstPage();
            do {
                uint32_t rowPosition = flip ? imageOffset + (height - h) * rowSize : imageOffset;
                for (uint16_t row = 0; row < h; row++, rowPosition += rowSize) {
                    file.seek(rowPosition);
                    file.read(input_buffer, rowSize);
                    for (uint16_t col = 0; col < w; col++) {
                        // Process pixel data here...
                    }
                }
            } while (display.nextPage());
        }
    }

    file.close();
    if (!valid) {
        Serial.println("bitmap format not handled.");
    }
}

void draw_bmp(const char* filename) {
    drawBitmapFromSD_Buffered(filename, 0, 0, true, false, false);
}