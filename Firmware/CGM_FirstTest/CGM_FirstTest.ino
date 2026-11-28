/*
  CYBER Celestia CGM - first test: aiming + trigger + ADS
  Board: Arduino Nano ESP32 (Arduino IDE: Tools > Board > Arduino ESP32 Boards > Arduino Nano ESP32)
  Library: "LSM6" by Pololu (Sketch > Include Library > Manage Libraries, search LSM6)

  Wiring (solder straight to the Nano's holes, no headers):
    IMU VIN -> 3V3      IMU GND -> GND
    IMU SDA -> A4       IMU SCL -> A5
    Trigger microswitch: COM -> GND, NO -> D2
    ADS microswitch:     COM -> GND, NO -> D3

  What it does:
    - Plug it in and keep it STILL for 2 seconds while the orange LED is on
      (it measures the gyro's resting offset).
    - Turning the gun left/right moves the mouse left/right; tilting it
      up/down moves it up/down.
    - Trigger = left click, ADS = right click (held while pressed).
    - Unplugging it always stops it.

  If the cursor goes the wrong way, change YAW_SIGN / PITCH_SIGN below.
  If turning left/right moves the cursor up/down, swap GYRO_YAW and GYRO_PITCH.
*/

#include <Wire.h>
#include <LSM6.h>
#include "USB.h"
#include "USBHIDMouse.h"

USBHIDMouse Mouse;
LSM6 imu;

// ---------- settings you can change ----------
const int PIN_TRIGGER = D2;
const int PIN_ADS = D3;

const float COUNTS_PER_DEG = 15.0;  // mouse counts per degree you turn: higher = faster aim
const float DEADZONE_DPS = 1.5;     // ignore turning slower than this (hand shake, drift)

const int YAW_SIGN = -1;            // flip to 1 if left/right is backwards
const int PITCH_SIGN = -1;          // flip to 1 if up/down is backwards
#define GYRO_YAW   imu.g.z          // which gyro axis is left/right (board lies flat)
#define GYRO_PITCH imu.g.y          // which is up/down; try imu.g.x if this one is wrong
// ----------------------------------------------

const float DPS_PER_LSB = 0.035;    // 1000 deg/s range: 35 millidegrees/s per count
const unsigned long DEBOUNCE_MS = 8;

bool imuOk = false;
float biasYaw = 0, biasPitch = 0;
float carryX = 0, carryY = 0;       // keeps the fractions of a count between loops
unsigned long lastMicros = 0;

struct Button {
  int pin;
  uint8_t mouseButton;
  bool down;
  bool lastRead;
  unsigned long changedAt;
};
Button trigger = {PIN_TRIGGER, MOUSE_LEFT, false, false, 0};
Button ads = {PIN_ADS, MOUSE_RIGHT, false, false, 0};

void updateButton(Button &b) {
  bool pressed = digitalRead(b.pin) == LOW;   // pressed switch connects the pin to GND
  unsigned long now = millis();
  if (pressed != b.lastRead) {                // still bouncing: restart the timer
    b.lastRead = pressed;
    b.changedAt = now;
  }
  if (pressed != b.down && now - b.changedAt >= DEBOUNCE_MS) {
    b.down = pressed;
    if (pressed) Mouse.press(b.mouseButton);
    else Mouse.release(b.mouseButton);
  }
}

void calibrate() {
  const int n = 400;                          // about 2 seconds
  long sumYaw = 0, sumPitch = 0;
  for (int i = 0; i < n; i++) {
    imu.read();
    sumYaw += GYRO_YAW;
    sumPitch += GYRO_PITCH;
    delay(5);
  }
  biasYaw = sumYaw / (float)n;
  biasPitch = sumPitch / (float)n;
}

int takeWhole(float &carry) {
  int whole = (int)carry;                     // move by whole counts only
  whole = constrain(whole, -127, 127);
  carry -= whole;
  return whole;
}

void setup() {
  pinMode(PIN_TRIGGER, INPUT_PULLUP);
  pinMode(PIN_ADS, INPUT_PULLUP);
  pinMode(LED_BUILTIN, OUTPUT);

  Mouse.begin();
  USB.begin();
  delay(1000);                                // let the PC finish recognising it

  Wire.begin();
  imuOk = imu.init();
  if (imuOk) {
    imu.enableDefault();
    imu.writeReg(LSM6::CTRL2_G, 0b01011000);  // gyro 208 Hz, 1000 deg/s (fast flicks fit)
    digitalWrite(LED_BUILTIN, HIGH);          // hold still...
    delay(300);
    calibrate();
    digitalWrite(LED_BUILTIN, LOW);
  }
  lastMicros = micros();
}

void loop() {
  updateButton(trigger);
  updateButton(ads);

  unsigned long now = micros();
  float dt = (now - lastMicros) / 1e6;
  lastMicros = now;

  if (imuOk) {
    imu.read();
    float yaw = (GYRO_YAW - biasYaw) * DPS_PER_LSB;       // degrees per second
    float pitch = (GYRO_PITCH - biasPitch) * DPS_PER_LSB;
    if (fabs(yaw) < DEADZONE_DPS) yaw = 0;
    if (fabs(pitch) < DEADZONE_DPS) pitch = 0;
    carryX += YAW_SIGN * yaw * dt * COUNTS_PER_DEG;
    carryY += PITCH_SIGN * pitch * dt * COUNTS_PER_DEG;
    int dx = takeWhole(carryX);
    int dy = takeWhole(carryY);
    if (dx != 0 || dy != 0) Mouse.move(dx, dy);
  } else {
    // No IMU found: blink the orange LED so you know to check its 4 wires
    digitalWrite(LED_BUILTIN, (millis() / 250) % 2);
  }
  delay(5);
}
