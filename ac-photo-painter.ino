#include <Arduino.h>
#include <GxEPD2_7C.h>
#include <PCF85063A.h>
#include <SD.h>
#include <SPI.h>
#include <time.h>

// SD Card pins.
const uint32_t SDCARD_SCK = 2;
const uint32_t SDCARD_MISO = 4;
const uint32_t SDCARD_MOSI = 3;
const uint32_t SDCARD_CS = 5;

// EPD pins.
const uint32_t EPD_RST = 12;
const uint32_t EPD_DC = 8;
const uint32_t EPD_CS = 9;
const uint32_t EPD_BUSY = 13;
const uint32_t EPD_SCK = 10;
const uint32_t EPD_MOSI = 11;
const uint32_t EPD_MISO = 12;  // Not used.
const uint32_t EPD_POWER_EN = 16;

// RTC pins.
const uint32_t RTC_INT = 6;
const uint32_t RTC_SDA = 14;
const uint32_t RTC_SCL = 15;

// General pins.
const uint32_t CHARGE_STATE = 17;  // Battery charging indicator (low is charging; high is not charging).
const uint32_t BAT_ENABLE = 18;    // Battery power control (high is enabled; low turns off the power).
const uint32_t USER_BUTTON = 19;   // User button (low is button pressed, or the auto-switch is enabled).
const uint32_t PWR_MODE = 23;      // Power mode (mystery pin).
const uint32_t VBUS_STATE = 24;    // USB bus power (high means there is power).
const uint32_t RED_LED = 25;       // Activity LED: red.
const uint32_t GREEN_LED = 26;     // Power LED: green.
const uint32_t VSYS_ADC = 29;      // Analog pin for VSYS voltage.

#define GxEPD2_DISPLAY_CLASS GxEPD2_7C
#define GxEPD2_DRIVER_CLASS GxEPD2_730c_ACeP_730
#define MAX_DISPLAY_BUFFER_SIZE 5000
#define MAX_HEIGHT(EPD) \
    (EPD::HEIGHT <= (MAX_DISPLAY_BUFFER_SIZE) / (EPD::WIDTH / 2) ? EPD::HEIGHT : (MAX_DISPLAY_BUFFER_SIZE) / (EPD::WIDTH / 2))
GxEPD2_DISPLAY_CLASS<GxEPD2_DRIVER_CLASS, MAX_HEIGHT(GxEPD2_DRIVER_CLASS)> display(GxEPD2_DRIVER_CLASS(EPD_CS, EPD_DC, EPD_RST,
                                                                                                       EPD_BUSY));

PCF85063A rtc(Wire1);

static void epd_busy_callback(const void*);
static void do_period_housekeeping(void);
static void set_time(void);
static void set_alarm_time(int seconds);
static float get_battery_voltage(void);
static void dump_rtc_regs(void);
static void draw_bmp(const char* filename);
static void print_directory(File dir, int indent);

void setup() {
    //
    // Set up GPIO pins.
    //

    // This ensures that we have the full 4096 ADC resolution.
    analogReadResolution(12);
    // Read the analog pin once to prime it.
    get_battery_voltage();

    pinMode(CHARGE_STATE, INPUT_PULLUP);
    pinMode(USER_BUTTON, INPUT_PULLUP);
    pinMode(PWR_MODE, INPUT);
    pinMode(VBUS_STATE, INPUT);

    pinMode(BAT_ENABLE, OUTPUT);
    digitalWrite(BAT_ENABLE, HIGH);

    pinMode(RED_LED, OUTPUT);
    digitalWrite(RED_LED, LOW);

    pinMode(GREEN_LED, OUTPUT);
    digitalWrite(GREEN_LED, HIGH);

    //
    // Set up the watchdog timer.
    //

    watchdog_enable(8 * 1000, false);

    //
    // Set up the RTC.
    //

    Wire1.setSDA(RTC_SDA);
    Wire1.setSCL(RTC_SCL);
    Wire1.begin();

    // TODO(tboldt): Make this dependent on whether the RTC has a valid date. Also, maybe prompt for it?
    // set_time();

    set_alarm_time(30);

    //
    // Set up SD Card.
    //

    SPI.setMISO(SDCARD_MISO);
    SPI.setMOSI(SDCARD_MOSI);
    SPI.setSCK(SDCARD_SCK);
    // Don't use hardware CS.
    SPI.begin(false);

    //
    // Set up E-paper display.
    //

    SPI1.setMISO(EPD_MISO);
    SPI1.setMOSI(EPD_MOSI);
    SPI1.setSCK(EPD_SCK);
    // Don't use hardware CS.
    SPI1.begin(false);

    display.epd2.selectSPI(SPI1, SPISettings(4000000, MSBFIRST, SPI_MODE0));
    // Enable EPD power.
    pinMode(EPD_POWER_EN, OUTPUT);
    digitalWrite(EPD_POWER_EN, HIGH);

    display.epd2.setBusyCallback(epd_busy_callback);
    display.init(115200, true, 2, false);

    // Let the SD library control the CS PIN.
    SD.begin(SDCARD_CS);
}

void loop() {
    do_period_housekeeping();

    // Turn on activity indicator.
    digitalWrite(RED_LED, HIGH);
    Serial.println("BEGIN WORK");

    if (SD.exists("/")) {
        Serial.println("Yes it exists.");
    } else {
        Serial.println("Nope");
    }

    // File root = SD.open("/");
    // print_directory(root, 0);
    // root.close();

    //XXX draw_bmp("/pic/charlie_scale_output.bmp");

    // Turn off the battery power.
    digitalWrite(BAT_ENABLE, LOW);
    // This line will never be reached when running on battery power.

    // Turn off activity indicator.
    Serial.println("END WORK");
    digitalWrite(RED_LED, LOW);

    // Wait for user to press button.
    bool button_pressed = false;
    while (!button_pressed) {
        do_period_housekeeping();
        for (int i = 0; i < 10; i++) {
            if (!digitalRead(USER_BUTTON)) {
                Serial.println("Button press detected!");
                button_pressed = true;
                break;
            }
            delay(100);
        }
    }
}

// This is called when the EPD is busy.
static void epd_busy_callback(const void*) {
    // Feed the watchdog, so it doesn't bite us.
    watchdog_update();
}

// Do periodic tasks.
static void do_period_housekeeping(void) {
    // Feed the watchdog, so it doesn't bite us.
    watchdog_update();

    // If the battery is low, disable the alarm, alert the user, and power off.
    if (get_battery_voltage() < 3.1f) {
        // Disable alarm.
        set_alarm_time(-1);

        // Flash 5 times to indicate a problem.
        for (int i = 0; i < 5; i++) {
            digitalWrite(RED_LED, HIGH);
            delay(300);
            digitalWrite(RED_LED, LOW);
            delay(300);
        }

        // Turn off the battery power.
        digitalWrite(BAT_ENABLE, LOW);
        // This line will never be reached when running on battery power.
    }

    // Turn on the green LED if the battery is charging.
    if (digitalRead(CHARGE_STATE) == LOW) {
        // Battery is charging.
        digitalWrite(GREEN_LED, HIGH);
    } else {
        // Battery is not charging.
        digitalWrite(GREEN_LED, LOW);
    }
}

// This is useful to get the RTC time set to a known value.
static void set_time(void) {
    struct tm t;
    t.tm_year = 2025 - 1900;  // Year since 1900
    t.tm_mon = 3 - 1;         // Month (0-11)
    t.tm_mday = 29;           // Day of the month (1-31)
    t.tm_hour = 12;           // Hour (0-23)
    t.tm_min = 37;            // Minutes (0-59)
    t.tm_sec = 0;             // Seconds (0-59)
    rtc.set(&t);
}

// Set an alarm for some time in the future.
//
// Maximum time is 24 hours from now.
// A negative value disables all alarms.
//
// Note: 0x80 is used to disable a field.
static void set_alarm_time(int seconds) {
    if (seconds >= 0) {
        time_t tt = rtc.time(NULL) + seconds;
        struct tm* t = localtime(&tt);
        rtc.alarm(PCF85063A::HOUR, t->tm_hour);
        rtc.alarm(PCF85063A::MINUTE, t->tm_min);
        rtc.alarm(PCF85063A::SECOND, t->tm_sec);
    } else {
        // Disable all alarms.
        rtc.alarm(PCF85063A::HOUR, 0x80);
        rtc.alarm(PCF85063A::MINUTE, 0x80);
        rtc.alarm(PCF85063A::SECOND, 0x80);
    }

    // We don't use date or day of week.
    rtc.alarm(PCF85063A::WEEKDAY, 0x80);
    rtc.alarm(PCF85063A::DAY, 0x80);

    // Also, clear any pending alarm.
    rtc.int_clear();
}

// Calculate VSYS, assuming a 12-bit ADC, 3.3Vref, and 3x voltage divider.
// When on battery, VBAT == VSYS.
static float get_battery_voltage(void) {
    float vsys = analogRead(VSYS_ADC) * 3.3f / 4096.0f * 3.0f;
    // Serial.printf("VBAT: %f V", vsys);
    // Serial.println();
    return vsys;
}

// Dump the I2C registers of the RTC.
static void dump_rtc_regs(void) {
    uint8_t data[16];
    rtc.reg_r(0, data, 16);
    for (int i = 0; i < 16; i++) {
        Serial.printf("%02x: %02x", i, data[i]);
        Serial.println();
    }
}

// XXXX
void drawBitmapFromSD_Buffered(const char* filename, int16_t x, int16_t y, bool with_color, bool partial_update, bool overwrite);

static void draw_bmp(const char* filename) {
    drawBitmapFromSD_Buffered(filename, 0, 0, true, false, false);
    // File file = SD.open(filename);
    // if (!file) {
    //     Serial.println("Failed to open file for reading");
    //     return;
    // }

    // display.setFullWindow();
    // display.firstPage();
    // do {
    //     display.drawBitmapFile(file, 0, 0);
    // } while (display.nextPage());

    // file.close();

    // Wait for the display to finish.
    // while (display.isBusy()) {
    //     // Feed the watchdog, so it doesn't bite us.
    //     watchdog_update();
    //     delay(100);
    // }
}

static void print_directory(File dir, int indent) {
    while (true) {
        File entry = dir.openNextFile();
        if (!entry) {
            if (indent == 0) {
                Serial.println("** Done **");
            }
            return;
        }

        for (uint8_t i = 0; i < indent; i++) {
            Serial.print("  ");
        }

        Serial.print(entry.name());

        if (entry.isDirectory()) {
            Serial.println("/");
            print_directory(entry, indent + 1);
        } else {
            Serial.print("    ");
            Serial.println(entry.size(), DEC);
        }

        entry.close();
    }
}

uint16_t read16(File& f) {
    // BMP data is stored little-endian, same as Arduino.
    uint16_t result;
    ((uint8_t*)&result)[0] = f.read();  // LSB
    ((uint8_t*)&result)[1] = f.read();  // MSB
    return result;
}

uint32_t read32(File& f) {
    // BMP data is stored little-endian, same as Arduino.
    uint32_t result;
    ((uint8_t*)&result)[0] = f.read();  // LSB
    ((uint8_t*)&result)[1] = f.read();
    ((uint8_t*)&result)[2] = f.read();
    ((uint8_t*)&result)[3] = f.read();  // MSB
    return result;
}


//static const uint16_t input_buffer_pixels = 20; // may affect performance
static const uint16_t input_buffer_pixels = 800; // may affect performance

static const uint16_t max_row_width = 1448; // for up to 6" display 1448x1072
static const uint16_t max_palette_pixels = 256; // for depth <= 8

uint8_t input_buffer[3 * input_buffer_pixels]; // up to depth 24
uint8_t output_row_mono_buffer[max_row_width / 8]; // buffer for at least one row of b/w bits
uint8_t output_row_color_buffer[max_row_width / 8]; // buffer for at least one row of color bits
uint8_t mono_palette_buffer[max_palette_pixels / 8]; // palette buffer for depth <= 8 b/w
uint8_t color_palette_buffer[max_palette_pixels / 8]; // palette buffer for depth <= 8 c/w
uint16_t rgb_palette_buffer[max_palette_pixels]; // palette buffer for depth <= 8 for buffered graphics, needed for 7-color display

void drawBitmapFromSD_Buffered(const char* filename, int16_t x, int16_t y, bool with_color, bool partial_update, bool overwrite) {
    File file;
    bool valid = false;  // valid format to be handled
    bool flip = true;    // bitmap is stored bottom-to-top
    bool has_multicolors = (display.epd2.panel == GxEPD2::ACeP565) || (display.epd2.panel == GxEPD2::GDEY073D46);
    uint32_t startTime = millis();
    if ((x >= display.width()) || (y >= display.height())) return;
    Serial.println();
    Serial.print("Loading image '");
    Serial.print(filename);
    Serial.println('\'');

    file = SD.open(filename);
    if (!file) {
        Serial.print("File not found");
        return;
    }

    // Parse BMP header
    if (read16(file) == 0x4D42)  // BMP signature
    {
        uint32_t fileSize = read32(file);
        uint32_t creatorBytes = read32(file);
        (void)creatorBytes;                   // unused
        uint32_t imageOffset = read32(file);  // Start of image data
        uint32_t headerSize = read32(file);
        uint32_t width = read32(file);
        int32_t height = (int32_t)read32(file);
        uint16_t planes = read16(file);
        uint16_t depth = read16(file);  // bits per pixel
        uint32_t format = read32(file);
        if ((planes == 1) && ((format == 0) || (format == 3)))  // uncompressed is handled, 565 also
        {
            Serial.print("File size: ");
            Serial.println(fileSize);
            Serial.print("Image Offset: ");
            Serial.println(imageOffset);
            Serial.print("Header size: ");
            Serial.println(headerSize);
            Serial.print("Bit Depth: ");
            Serial.println(depth);
            Serial.print("Image size: ");
            Serial.print(width);
            Serial.print('x');
            Serial.println(height);
            // BMP rows are padded (if needed) to 4-byte boundary
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
            // if (w <= max_row_width) // handle with direct drawing
            {
                valid = true;
                uint8_t bitmask = 0xFF;
                uint8_t bitshift = 8 - depth;
                uint16_t red, green, blue;
                bool whitish = false;
                bool colored = false;
                if (depth == 1) with_color = false;
                if (depth <= 8) {
                    if (depth < 8) bitmask >>= depth;
                    // file.seek(54); //palette is always @ 54
                    file.seek(imageOffset - (4 << depth));  // 54 for regular, diff for colorsimportant
                    for (uint16_t pn = 0; pn < (1 << depth); pn++) {
                        blue = file.read();
                        green = file.read();
                        red = file.read();
                        file.read();
                        whitish = with_color ? ((red > 0x80) && (green > 0x80) && (blue > 0x80))
                                             : ((red + green + blue) > 3 * 0x80);     // whitish
                        colored = (red > 0xF0) || ((green > 0xF0) && (blue > 0xF0));  // reddish or yellowish?
                        if (0 == pn % 8) mono_palette_buffer[pn / 8] = 0;
                        mono_palette_buffer[pn / 8] |= whitish << pn % 8;
                        if (0 == pn % 8) color_palette_buffer[pn / 8] = 0;
                        color_palette_buffer[pn / 8] |= colored << pn % 8;
                        rgb_palette_buffer[pn] = ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | ((blue & 0xF8) >> 3);
                    }
                }
                if (partial_update)
                    display.setPartialWindow(x, y, w, h);
                else
                    display.setFullWindow();
                display.firstPage();
                do {
                    // if (!overwrite) display.fillScreen(GxEPD_WHITE);
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
                            // Time to read more pixel data?
                            if (in_idx >= in_bytes)  // ok, exact match for 24bit also (size IS multiple of 3)
                            {
                                in_bytes =
                                    file.read(input_buffer, in_remain > sizeof(input_buffer) ? sizeof(input_buffer) : in_remain);
                                in_remain -= in_bytes;
                                in_idx = 0;
                            }
                            switch (depth) {
                                case 32:
                                    blue = input_buffer[in_idx++];
                                    green = input_buffer[in_idx++];
                                    red = input_buffer[in_idx++];
                                    in_idx++;  // skip alpha
                                    whitish = with_color ? ((red > 0x80) && (green > 0x80) && (blue > 0x80))
                                                         : ((red + green + blue) > 3 * 0x80);     // whitish
                                    colored = (red > 0xF0) || ((green > 0xF0) && (blue > 0xF0));  // reddish or yellowish?
                                    color = ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | ((blue & 0xF8) >> 3);
                                    break;
                                case 24:
                                    blue = input_buffer[in_idx++];
                                    green = input_buffer[in_idx++];
                                    red = input_buffer[in_idx++];
                                    whitish = with_color ? ((red > 0x80) && (green > 0x80) && (blue > 0x80))
                                                         : ((red + green + blue) > 3 * 0x80);     // whitish
                                    colored = (red > 0xF0) || ((green > 0xF0) && (blue > 0xF0));  // reddish or yellowish?
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
                                    whitish = with_color ? ((red > 0x80) && (green > 0x80) && (blue > 0x80))
                                                         : ((red + green + blue) > 3 * 0x80);     // whitish
                                    colored = (red > 0xF0) || ((green > 0xF0) && (blue > 0xF0));  // reddish or yellowish?
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
                                    whitish = mono_palette_buffer[pn / 8] & (0x1 << pn % 8);
                                    colored = color_palette_buffer[pn / 8] & (0x1 << pn % 8);
                                    in_byte <<= depth;
                                    in_bits -= depth;
                                    color = rgb_palette_buffer[pn];
                                } break;
                            }
                            if (with_color && has_multicolors) {
                                // keep color
                            } else if (whitish) {
                                color = GxEPD_WHITE;
                            } else if (colored && with_color) {
                                color = GxEPD_COLORED;
                            } else {
                                color = GxEPD_BLACK;
                            }
                            uint16_t yrow = y + (flip ? h - row - 1 : row);
                            display.drawPixel(x + col, yrow, color);
                        }  // end pixel
                    }  // end line
                    Serial.print("page loaded in ");
                    Serial.print(millis() - startTime);
                    Serial.println(" ms");
                    watchdog_update();
                } while (display.nextPage());
                Serial.print("loaded in ");
                Serial.print(millis() - startTime);
                Serial.println(" ms");
                watchdog_update();
            }
        }
    }
    file.close();
    if (!valid) {
        Serial.println("bitmap format not handled.");
    }
}