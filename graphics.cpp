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

    Serial.println();
    Serial.print("Loading image '");
    Serial.print(filename);
    Serial.println('\'');

    File file = SD.open(filename);
    if (!file) {
        Serial.println("File not found");
        return;
    }

    if (read16(file) == 0x4D42) {  // BMP signature
        const auto fileSize = read32(file);
        file.seek(10);  // Skip to image offset
        const auto imageOffset = read32(file);
        file.seek(18);  // Skip to width and height
        const auto width = read32(file);
        auto height = static_cast<int32_t>(read32(file));
        file.seek(28);  // Skip to depth
        const auto depth = read16(file);
        const auto format = read32(file);

        if (depth == 16 || depth == 24 || depth == 32) {
            const auto rowSize = ((width * depth + 31) / 32) * 4;
            if (height < 0) {
                height = -height;
                flip = false;
            }

            auto w = std::min<uint16_t>(width, display.width() - x);
            auto h = std::min<uint16_t>(height, display.height() - y);

            valid = true;

            display.setFullWindow();
            display.setRotation(2);
            display.fillScreen(GxEPD_WHITE);

            for (uint16_t row = 0; row < h; ++row) {
                const auto rowPosition = flip ? imageOffset + (height - row - 1) * rowSize : imageOffset + row * rowSize;
                file.seek(rowPosition);

                file.read(input_buffer, std::min<size_t>(rowSize, sizeof(input_buffer)));

                for (uint16_t col = 0; col < w; ++col) {
                    uint16_t color = GxEPD_WHITE;

                    switch (depth) {
                        case 32: {
                            const auto blue = input_buffer[col * 4];
                            const auto green = input_buffer[col * 4 + 1];
                            const auto red = input_buffer[col * 4 + 2];
                            color = ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | ((blue & 0xF8) >> 3);
                            break;
                        }
                        case 24: {
                            const auto blue = input_buffer[col * 3];
                            const auto green = input_buffer[col * 3 + 1];
                            const auto red = input_buffer[col * 3 + 2];
                            color = ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | ((blue & 0xF8) >> 3);
                            break;
                        }
                        case 16: {
                            const auto lsb = input_buffer[col * 2];
                            const auto msb = input_buffer[col * 2 + 1];
                            color = (msb << 8) | lsb;
                            break;
                        }
                    }

                    display.drawPixel(x + col, y + row, color);
                }
            }

            display.display();

            Serial.println("Image loaded successfully.");
        }
    }

    file.close();
    if (!valid) {
        Serial.println("Unsupported bitmap format.");
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