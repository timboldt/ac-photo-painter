#include <Arduino.h>
#include <PCF85063A.h>
#include <SD.h>
#include <SPI.h>
#include <time.h>

PCF85063A rtc(Wire1);

// SD Card pins.
const uint32_t SDCARD_SCK = 2;
const uint32_t SDCARD_MISO = 4;
const uint32_t SDCARD_MOSI = 3;
const uint32_t SDCARD_CS = 5;

// RTC pins.
const uint32_t RTC_SDA = 14;
const uint32_t RTC_SCL = 15;

// General GPIOs.
const uint32_t CHARGE_STATE = 17;  // Battery charging indicator (low is charging; high is not charging).
const uint32_t BAT_ENABLE = 18;    // Battery power control (high is enabled; low turns off the power).
const uint32_t USER_BUTTON = 19;   // User button (low is button pressed, or the auto-switch is enabled).
const uint32_t PWR_MODE = 23;      // Power mode (mystery pin).
const uint32_t VBUS_STATE = 24;    // USB bus power (high means there is power).
const uint32_t RED_LED = 25;       // Activity LED: red.
const uint32_t GREEN_LED = 26;     // Power LED: green.

static void do_period_housekeeping(void);
static void set_time(void);
static void set_alarm_time(int seconds);
static float get_battery_voltage(void);
static void dump_rtc_regs(void);

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

    delay(2000);

    // if (digitalRead(VBUS_STATE) == HIGH) {
    //     // USB bus power is present.
    //     Serial.println("USB bus power is present.");
    // } else {
    //     // USB bus power is not present.
    //     Serial.println("USB bus power is not present.");
    // }
    // if (digitalRead(PWR_MODE) == HIGH) {
    //     // Power mode is high.
    //     Serial.println("Power mode is high.");
    // } else {
    //     // Power mode is low.
    //     Serial.println("Power mode is low.");
    // }

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
    float vsys = analogRead(A3) * 3.3f / 4096.0f * 3.0f;
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