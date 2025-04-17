#include <Arduino.h>
#include <PCF85063A.h>
#include <SD.h>
#include <SPI.h>
#include <time.h>

#include "graphics.h"

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

GxEPD2_7C<GxEPD2_730c_ACeP_730, GxEPD2_730c_ACeP_730::HEIGHT> display(GxEPD2_DRIVER_CLASS(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

PCF85063A rtc(Wire1);

static void epd_busy_callback(const void*);
static void do_period_housekeeping(void);
static void set_time(void);
static void set_alarm_time(int seconds);
static float get_battery_voltage(void);
static void dump_rtc_regs(void);
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

    set_alarm_time(4 * 60 * 60);

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

    // Seed the random number generator.
    randomSeed(get_rand_32());
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

    const char* images[] = {
        "/pic/escherlizard_cut_output.bmp",     "/pic/renoirparis_scale_output.bmp",        "/pic/fantasycastle_scale_output.bmp",
        "/pic/castleoverhead_scale_output.bmp", "/pic/jacknicholson_scale_output.bmp",      "/pic/colorrelativity_scale_output.bmp",
        "/pic/charlie_cut_output.bmp",          "/pic/mountainpathspring_scale_output.bmp", "/pic/chinesebird_cut_output.bmp",
        "/pic/rembrandttree_scale_output.bmp",  "/pic/laurelhardy_cut_output.bmp",          "/pic/escherwaterfall_cut_output.bmp",
        "/pic/charlie_scale_output.bmp"};

    // Select a random image from the array.
    int randomIndex = random(0, sizeof(images) / sizeof(images[0]));

    // Display the randomly selected image.
    draw_bmp(images[randomIndex]);

    // Turn off the battery power.
    digitalWrite(BAT_ENABLE, LOW);

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
    // print_directory(SD.open("/pic"), 0);
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