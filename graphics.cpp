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

void drawBitmapFromSD_Buffered(const char* filename, int16_t x, int16_t y) {
    bool valid = false;
    bool flip = true;
    uint32_t startTime = millis();

    Serial.println();
    Serial.print("Loading image '");
    Serial.print(filename);
    Serial.println('"');

    File file = SD.open(filename);
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

            display.setFullWindow();
            display.setRotation(2);
            display.fillScreen(GxEPD_WHITE);

            uint32_t rowPosition = flip ? imageOffset + (height - h) * rowSize : imageOffset;
            for (uint16_t row = 0; row < h; row++, rowPosition += rowSize)  // for each line
            {
                uint32_t in_remain = rowSize;
                uint32_t in_idx = 0;
                uint32_t in_bytes = 0;
                uint8_t in_byte = 0;  // for depth <= 8
                uint8_t in_bits = 0;  // for depth <= 8
                uint16_t color = GxEPD_WHITE;
                file.seek(rowPosition);
                for (uint16_t col = 0; col < w; col++)  // for each pixel
                {
                    uint8_t bitmask = 0xFF;
                    uint8_t bitshift = 8 - depth;
                    uint16_t red, green, blue;

                    // Time to read more pixel data?
                    if (in_idx >= in_bytes)  // ok, exact match for 24bit also (size IS multiple of 3)
                    {
                        in_bytes = file.read(input_buffer, in_remain > sizeof(input_buffer) ? sizeof(input_buffer) : in_remain);
                        in_remain -= in_bytes;
                        in_idx = 0;
                    }
                    switch (depth) {
                        case 32:
                            blue = input_buffer[in_idx++];
                            green = input_buffer[in_idx++];
                            red = input_buffer[in_idx++];
                            in_idx++;  // skip alpha
                            // whitish = with_color ? ((red > 0x80) && (green > 0x80) && (blue > 0x80))
                            //                      : ((red + green + blue) > 3 * 0x80);     // whitish
                            // colored = (red > 0xF0) || ((green > 0xF0) && (blue > 0xF0));  // reddish or yellowish?
                            color = ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | ((blue & 0xF8) >> 3);
                            break;
                        case 24:
                            blue = input_buffer[in_idx++];
                            green = input_buffer[in_idx++];
                            red = input_buffer[in_idx++];
                            // whitish = with_color ? ((red > 0x80) && (green > 0x80) && (blue > 0x80))
                            //                      : ((red + green + blue) > 3 * 0x80);     // whitish
                            // colored = (red > 0xF0) || ((green > 0xF0) && (blue > 0xF0));  // reddish or yellowish?
                            color = ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | ((blue & 0xF8) >> 3);
                            break;
                        case 16: {
                            uint8_t lsb = input_buffer[in_idx++];
                            uint8_t msb = input_buffer[in_idx++];
                            if (format == 0)  // 555
                            {
                                blue = (lsb & 0x1F) << 3;
                                green = ((msb & 0x03) << 6) | ((lsb & 0xE0) >> 2);
                                red = (msb & 0x7C) << 1;
                                color = ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | ((blue & 0xF8) >> 3);
                            } else  // 565
                            {
                                blue = (lsb & 0x1F) << 3;
                                green = ((msb & 0x07) << 5) | ((lsb & 0xE0) >> 3);
                                red = (msb & 0xF8);
                                color = (msb << 8) | lsb;
                            }
                            // whitish = with_color ? ((red > 0x80) && (green > 0x80) && (blue > 0x80))
                            //                      : ((red + green + blue) > 3 * 0x80);     // whitish
                            // colored = (red > 0xF0) || ((green > 0xF0) && (blue > 0xF0));  // reddish or yellowish?
                        } break;
                        case 1:
                        case 2:
                        case 4:
                        case 8: {
                            if (0 == in_bits) {
                                in_byte = input_buffer[in_idx++];
                                in_bits = 8;
                            }
                            uint16_t pn = (in_byte >> bitshift) & bitmask;
                            // whitish = mono_palette_buffer[pn / 8] & (0x1 << pn % 8);
                            // colored = color_palette_buffer[pn / 8] & (0x1 << pn % 8);
                            in_byte <<= depth;
                            in_bits -= depth;
                            color = rgb_palette_buffer[pn];
                        } break;
                    }
                    uint16_t yrow = y + (flip ? h - row - 1 : row);
                    display.drawPixel(x + col, yrow, color);
                }  // end pixel
                watchdog_update();
            }  // end line

            display.display();

            Serial.print("page loaded in ");
            Serial.print(millis() - startTime);
            Serial.println(" ms");
        }
    }

    file.close();
    if (!valid) {
        Serial.println("bitmap format not handled.");
    }
}

void draw_bmp(const char* filename) {
    drawBitmapFromSD_Buffered(filename, 0, 0);

    // display.setFullWindow();
    // display.setRotation(2);
    // display.fillScreen(GxEPD_WHITE);
    // display.fillRect(10, 10, 100, 50, GxEPD_RED);
    // // display.fillTriangle(50, 150, 100, 200, 150, 150, GxEPD_GREEN);
    // display.setTextColor(GxEPD_BLACK);
    // display.setCursor(100, 175);  // Adjust the position to be to the right of the triangle
    // display.setTextSize(6);
    // time_t now = time(nullptr);
    // struct tm* localTime = localtime(&now);
    // char timeString[20];
    // strftime(timeString, sizeof(timeString), "%Y-%m-%d %H:%M:%S", localTime);
    // display.print(timeString);
    // display.display();
}