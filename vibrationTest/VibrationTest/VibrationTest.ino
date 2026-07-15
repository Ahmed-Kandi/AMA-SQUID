#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

int vPin = 5;
int tooLoudPin = 6;
int tooQuietPin = 7;
int tooFastPin = 8;
int tooSlowPin = 9;


void setup() {
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);

  ledcAttach(vPin, 20000, 8);
  pinMode(tooLoudPin, INPUT_PULLUP);
  pinMode(tooQuietPin, INPUT_PULLUP);
  pinMode(tooFastPin, INPUT_PULLUP);
  pinMode(tooSlowPin, INPUT_PULLUP);
  Serial.begin(9600);

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.display();
}

void loop() {
  if (digitalRead(tooLoudPin) == 0) {
    tooLoud();
  } else if (digitalRead(tooQuietPin) == 0) {
    tooQuiet();
  } else if (digitalRead(tooFastPin) == 0) {
    tooFast();
  } else if (digitalRead(tooSlowPin) == 0) {
    tooSlow();
  }

  delay(20);
}

void tooLoud() {
  sendToDisplay("Too Loud!");
  for (int i = 255; i > 32; i -= 8) {
    ledcWrite(vPin, i);
    delay(50);
  }
  ledcWrite(vPin, 0);
}

void tooQuiet() {
  sendToDisplay("Too Quiet!");
  for (int i = 32; i < 255; i += 8) {
    ledcWrite(vPin, i);
    delay(50);
  }
  ledcWrite(vPin, 0);
}

void tooFast() {
  sendToDisplay("Too Fast!");
  for (int i = 0; i < 3; i++) {
    ledcWrite(vPin, 255);
    delay(100);
    ledcWrite(vPin, 0);
    delay(75);
  }
  for (int i = 100; i <= 500; i += 100) {
    ledcWrite(vPin, 255);
    delay(100);
    ledcWrite(vPin, 0);
    delay(i);
  }
}

void tooSlow() {
  sendToDisplay("Too Slow!");
  for (int i = 500; i >= 100; i -= 100) {
  ledcWrite(vPin, 255);
  delay(100);
  ledcWrite(vPin, 0);
  delay(i);
  }
  for (int i = 0; i < 3; i++) {
    ledcWrite(vPin, 255);
    delay(100);
    ledcWrite(vPin, 0);
    delay(75);
  }
}

void sendToDisplay(String message) {
  display.clearDisplay();
  display.setCursor(0, 0);
  Serial.print("Displaying: ");
  Serial.println(message);
  display.println(message);
  display.display();
}
