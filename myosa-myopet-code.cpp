/*******************
 * MYOSA – FINAL ALL-IN-ONE CODE
 * Board : ESP32-WROOM-DA
 *******************/

#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include "driver/dac.h"

#include <SparkFun_APDS9960.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP085_U.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ===================== PINS =====================
#define SDA_PIN    21
#define SCL_PIN    22
#define SD_CS      5
#define AUDIO_DAC  DAC_CHANNEL_1   // GPIO25
#define BUZZER_PIN 27
#define MODE_BTN   16
#define RESET_BTN  4

// ===================== OLED =====================
Adafruit_SSD1306 display(128, 64, &Wire, -1);

// ===================== SENSORS ==================
SparkFun_APDS9960 apds;
Adafruit_MPU6050 mpu;
Adafruit_BMP085_Unified bmp(10085);

// ===================== MODES ====================
enum Mode { MODE_ABC, MODE_COLOR, MODE_EMOJI, MODE_TEMP };
Mode currentMode = MODE_ABC;

// ===================== VARIABLES =================
char currentLetter = 'A';
bool lastHandDetected = false;

uint16_t r, g, b;
String lastColor = "";
unsigned long lastColorTime = 0;

unsigned long lastButtonTime = 0;
unsigned long resetPressTime = 0;
bool modeBtnLocked = false;

// Emoji variables
String currentMood = "Happy";
bool isDizzy = false;
unsigned long dizzyStartTime = 0;
String lastMood = "";

// Entry flags
bool firstABC = true;
bool firstColor = true;
bool firstEmoji = true;
bool firstTemp = true;

// ===================== CONSTANTS =================
#define COLOR_AUDIO_GAP  2500
#define MIN_LIGHT_THRESHOLD 50

// ===================== AUDIO =====================
void playWav(const char *filename) {
  File file = SD.open(filename);
  if (!file) return;

  for (int i = 0; i < 44; i++) file.read(); // WAV header

  while (file.available()) {
    dac_output_voltage(AUDIO_DAC, file.read());
    delayMicroseconds(125); // 8000 Hz
  }
  file.close();
}

// ===================== OLED HELPERS =====================
void showCenterText(String text, int size) {
  display.clearDisplay();
  display.setTextSize(size);
  display.setTextColor(SSD1306_WHITE);
  int16_t x1, y1; uint16_t w, h;
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((128 - w) / 2, (64 - h) / 2);
  display.print(text);
  display.display();
}

// ===================== EMOJI FACES =====================
void drawEmojiFace(String mood) {
  display.clearDisplay();
  
  if (mood == "Happy") {
    display.fillCircle(32, 25, 12, SSD1306_WHITE);
    display.fillCircle(96, 25, 12, SSD1306_WHITE);
    display.fillCircle(64, 45, 14, SSD1306_WHITE); 
    display.fillRect(40, 30, 48, 14, SSD1306_BLACK);
    
  } else if (mood == "Sad") {
    display.fillCircle(32, 25, 12, SSD1306_WHITE); 
    display.fillCircle(96, 25, 12, SSD1306_WHITE); 
    display.fillCircle(64, 55, 14, SSD1306_WHITE); 
    display.fillRect(40, 56, 48, 15, SSD1306_BLACK);
    
  } else if (mood == "Excited") {
    display.fillCircle(32, 32, 18, SSD1306_WHITE); 
    display.fillCircle(32, 32, 8, SSD1306_BLACK);
    display.fillCircle(96, 32, 18, SSD1306_WHITE); 
    display.fillCircle(96, 32, 8, SSD1306_BLACK);
    display.fillCircle(64, 55, 6, SSD1306_WHITE);
    
  } else if (mood == "Sleepy") {
    display.fillRect(20, 30, 24, 4, SSD1306_WHITE); 
    display.fillRect(84, 30, 24, 4, SSD1306_WHITE); 
    display.setCursor(100, 10);
    display.setTextSize(2);
    display.print("z");
    display.fillCircle(64, 50, 4, SSD1306_WHITE);
    
  } else if (mood == "Dizzy") {
    display.drawLine(20, 20, 44, 44, SSD1306_WHITE);
    display.drawLine(44, 20, 20, 44, SSD1306_WHITE);
    display.drawLine(84, 20, 108, 44, SSD1306_WHITE);
    display.drawLine(108, 20, 84, 44, SSD1306_WHITE);
    display.drawLine(50, 55, 78, 55, SSD1306_WHITE);
    
  } else if (mood == "Ouch") {
    display.drawLine(25, 20, 35, 30, SSD1306_WHITE);
    display.drawLine(35, 30, 25, 40, SSD1306_WHITE);
    display.drawLine(103, 20, 93, 30, SSD1306_WHITE);
    display.drawLine(93, 30, 103, 40, SSD1306_WHITE);
    display.drawCircle(64, 55, 8, SSD1306_WHITE);
    
  } else if (mood == "Hello") {
    display.fillCircle(32, 30, 12, SSD1306_WHITE);
    display.fillRect(84, 30, 24, 4, SSD1306_WHITE);
    display.setCursor(45, 55);
    display.setTextSize(1);
    display.print("Hi!");
  }

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print(mood);
  display.display();
}

// ===================== SETUP =====================
void setup() {
  Serial.begin(115200);
  Wire.begin(SDA_PIN, SCL_PIN);

  pinMode(MODE_BTN, INPUT_PULLUP);
  pinMode(RESET_BTN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  showCenterText("MYOSA", 2);

  SD.begin(SD_CS);
  dac_output_enable(AUDIO_DAC);
  delay(600);
  playWav("/HELLO.WAV");

  apds.init();
  apds.enableProximitySensor(false);
  apds.enableLightSensor(false);

  mpu.begin();
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);

  bmp.begin();
}

// ===================== LOOP =====================
void loop() {

  // ---------- RESET ----------
  if (digitalRead(RESET_BTN) == LOW) {
    if (resetPressTime == 0) resetPressTime = millis();
    if (millis() - resetPressTime > 800) ESP.restart();
  } else {
    resetPressTime = 0;
  }

  // ---------- MODE CHANGE (LOCKED) ----------
  if (digitalRead(MODE_BTN) == LOW && !modeBtnLocked) {
    modeBtnLocked = true;

    currentMode = (Mode)((currentMode + 1) % 4);

    firstABC = firstColor = firstEmoji = firstTemp = true;
    lastColor = "";
    lastColorTime = 0;
    isDizzy = false;
    lastMood = "";
  }

  if (digitalRead(MODE_BTN) == HIGH) {
    modeBtnLocked = false;
  }

  // ===================== ABC MODE =====================
  if (currentMode == MODE_ABC) {

    if (firstABC) {
      showCenterText("ALPHABET MODE", 1);
      playWav("/ALPHABET MODE.WAV");
      delay(300);
      showCenterText("A", 4);
      playWav("/A.WAV");
      currentLetter = 'A';
      lastHandDetected = false;
      firstABC = false;
    }

    uint8_t p;
    apds.readProximity(p);
    bool hand = p > 120;

    if (hand && !lastHandDetected) {
      currentLetter++;
      if (currentLetter > 'Z') currentLetter = 'A';
      showCenterText(String(currentLetter), 4);
      char f[7] = {'/', currentLetter, '.', 'W', 'A', 'V', 0};
      playWav(f);
    }
    lastHandDetected = hand;
  }

  // ===================== COLOR MODE =====================
  else if (currentMode == MODE_COLOR) {

    if (firstColor) {
      showCenterText("COLOR MODE", 1);
      playWav("/COLOR DETECTION MODE.WAV");
      delay(400);
      lastColor = "";
      lastColorTime = 0;
      firstColor = false;
    }

    apds.readRedLight(r);
    apds.readGreenLight(g);
    apds.readBlueLight(b);

    uint32_t sum = r + g + b;
    
    String color = "----";
    
    if (sum > MIN_LIGHT_THRESHOLD) {
      if ((r * 100 / sum) > 45) color = "RED";
      else if ((g * 100 / sum) > 45) color = "GREEN";
      else if ((b * 100 / sum) > 45) color = "BLUE";
    }

    if (color != "----" && color != lastColor &&
        millis() - lastColorTime > COLOR_AUDIO_GAP) {

      lastColor = color;
      lastColorTime = millis();
      showCenterText(color, 2);
      playWav(("/" + color + ".WAV").c_str());
    }
    
    if (color == "----" && lastColor != "") {
      if (millis() - lastColorTime > COLOR_AUDIO_GAP) {
        lastColor = "";
      }
    }
  }

  // ===================== EMOJI MODE =====================
  else if (currentMode == MODE_EMOJI) {

    if (firstEmoji) {
      showCenterText("EMOJI MODE", 1);
      delay(1000);
      isDizzy = false;
      lastMood = "";
      firstEmoji = false;
    }

    sensors_event_t a, g, t;
    mpu.getEvent(&a, &g, &t);

    // Check for shake or dizzy motion first
    float accelMag = sqrt(pow(a.acceleration.x, 2) + pow(a.acceleration.y, 2) + pow(a.acceleration.z, 2));
    
    if (accelMag > 25.0 || accelMag < 2.0) {
      isDizzy = true;
      dizzyStartTime = millis();
    }

    if (isDizzy && millis() - dizzyStartTime < 3000) {
      currentMood = "Dizzy";
      drawEmojiFace("Dizzy");
      lastMood = currentMood;
      delay(50);
      return;
    } else {
      isDizzy = false;
    }

    // Check for taps next
    if (a.acceleration.z > 20.0) {
      currentMood = "Ouch";
      drawEmojiFace("Ouch");
      lastMood = currentMood;
      delay(1000);
      return;
    }
    
    if (a.acceleration.z > 14.0) {
      currentMood = "Hello";
      drawEmojiFace("Hello");
      lastMood = currentMood;
      delay(800);
      return;
    }

    // Check tilt directions
    float x = a.acceleration.x;
    float y = a.acceleration.y;

    if (x > 3.0) {
      currentMood = "Sleepy";
    } else if (x < -3.0) {
      currentMood = "Excited";
    } else if (y > 3.0) {
      currentMood = "Sad";
    } else if (y < -3.0) {
      currentMood = "Happy";
    } else {
      currentMood = "Happy";
    }

    if (currentMood != lastMood) {
      drawEmojiFace(currentMood);
      
      if (currentMood == "Happy") {
        playWav("/Happy Mode.WAV");
      } else if (currentMood == "Sad") {
        playWav("/Sad mode.WAV");
      } else if (currentMood == "Sleepy") {
        playWav("/SLEEPY.WAV");
      }
      
      lastMood = currentMood;
    } else {
      drawEmojiFace(currentMood);
    }
  }

  // ===================== TEMP MODE =====================
  else if (currentMode == MODE_TEMP) {

    if (firstTemp) {
      showCenterText("TEMP MODE", 1);
      playWav("/Temperature Mode.WAV");
      firstTemp = false;
    }

    sensors_event_t event;
    bmp.getEvent(&event);
    if (event.pressure) {
      float t;
      bmp.getTemperature(&t);
      showCenterText(String(t, 1) + " C", 2);
    }
  }

  delay(60);
}