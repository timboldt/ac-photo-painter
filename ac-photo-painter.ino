#include <Arduino.h>
#include <PCF85063A.h>
#include <time.h>

PCF85063A rtc(Wire1);

// Original C code behavior:
//
// init:
//   stdio logging
//   epd spi
//   sd spi
//   rtc i2c
//   VBAT ADC on pin 29
//   gpio init:
//     4x epd pins
//     2x led pins
//     3x charge/battery pins
//     2x power control pins
// watchdog enable
// sleep 1s
// rtc init
// rtc set alarm
// enable charge state IRQ callback
// if battery is low:
//   disable alarm
//   flash low power led
//   turn power off
// led on
// read SD card
// if no main power
//    run display
//    turn power off
// in a loop:
//    wait for key press
//    run display

const uint32_t CHARGE_STATE = 17;  // Battery charging indicator (low is charging; high is not charging).
const uint32_t BAT_ENABLE = 18;    // Battery power control (high is enabled; low turns off the power).
const uint32_t USER_BUTTON = 19;   // User button (low is button pressed, or the auto-switch is enabled).
const uint32_t PWR_MODE = 23;      // Power mode (mystery pin).
const uint32_t VBUS_STATE = 24;    // USB bus power (high means there is power).
const uint32_t RED_LED = 25;       // Activity LED: red.
const uint32_t GREEN_LED = 26;     // Power LED: green.

static void set_time(void);
static void set_alarm_time(int seconds);

void setup() {
    //
    // Set up GPIO pins.
    //

    analogReadResolution(12);

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

    Wire1.setSDA(14);
    Wire1.setSCL(15);
    Wire1.begin();

    // TODO(tboldt): Make this dependent on whether the RTC has a valid date. Also, maybe prompt for it?
    // set_time();

    set_alarm_time(30);
}

void loop() {
    watchdog_update();

    // uint8_t data[18];
    // rtc.reg_r(0, data, 18);
    // for (int i = 0; i < 18; i++) {
    //     Serial.printf("%02x: %02x\n", i, data[i]);
    // }

    time_t current_time = rtc.time(NULL);
    Serial.print("time : ");
    Serial.print(current_time);
    Serial.print(", ");
    Serial.println(ctime(&current_time));

    float vsys = analogRead(A3) * 3.3f / 4096.0f * 3.0f;
    Serial.println(vsys);

    if (digitalRead(CHARGE_STATE) == LOW) {
        // Battery is charging.
        Serial.println("Battery is charging.");
    } else {
        // Battery is not charging.
        Serial.println("Battery is not charging.");
    }
    if (digitalRead(USER_BUTTON) == LOW) {
        // Button is pressed.
        Serial.println("Button is pressed.");
    } else {
        // Button is not pressed.
        Serial.println("Button is not pressed.");
    }
    if (digitalRead(VBUS_STATE) == HIGH) {
        // USB bus power is present.
        Serial.println("USB bus power is present.");
    } else {
        // USB bus power is not present.
        Serial.println("USB bus power is not present.");
    }
    if (digitalRead(PWR_MODE) == HIGH) {
        // Power mode is high.
        Serial.println("Power mode is high.");
    } else {
        // Power mode is low.
        Serial.println("Power mode is low.");
    }

    Serial.println("Green LED on, red LED off");
    digitalWrite(RED_LED, LOW);
    digitalWrite(GREEN_LED, HIGH);
    delay(1000);
    Serial.println("Green LED off, red LED on");
    digitalWrite(RED_LED, HIGH);
    digitalWrite(GREEN_LED, LOW);
    delay(1000);

    // Turn off the battery power.
    digitalWrite(BAT_ENABLE, LOW);
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
// Maximum time is 24 hours from now.
static void set_alarm_time(int seconds) {
    time_t tt = rtc.time(NULL) + seconds;
    struct tm* t = localtime(&tt);

    // Note: 0x80 is used to disable a field.
    rtc.alarm(PCF85063A::WEEKDAY, 0x80);
    rtc.alarm(PCF85063A::DAY, 0x80);
    rtc.alarm(PCF85063A::HOUR, t->tm_hour);
    rtc.alarm(PCF85063A::MINUTE, t->tm_min);
    rtc.alarm(PCF85063A::SECOND, t->tm_sec);

    // Also, clear any pending alarm.
    rtc.int_clear();
}