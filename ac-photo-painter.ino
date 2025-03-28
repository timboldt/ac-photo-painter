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

void setup() {
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

    Wire1.setSDA(14);
    Wire1.setSCL(15);
    Wire1.begin();

    // Temporary: Set the time.
    // struct tm now_tm;
    // now_tm.tm_year = 2025 - 1900;
    // now_tm.tm_mon = 3 - 1;  // It needs to be '3' if April
    // now_tm.tm_mday = 27;
    // now_tm.tm_hour = 22;
    // now_tm.tm_min = 7;
    // now_tm.tm_sec = 0;
    // rtc.set(&now_tm);
}

void loop() {
    time_t current_time = 0;

    current_time = rtc.time(NULL);
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
}
