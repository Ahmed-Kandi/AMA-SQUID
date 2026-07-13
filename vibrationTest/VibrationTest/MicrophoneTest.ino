#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// --- DISPLAY SETUP ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// --- PIN DEFINITIONS ---
const int micPin = A0;
const int vPin = 5; // Replace with your actual vibration motor pin

// Renamed your 4 button pins for their new jobs
const int btnMaxUpPin = 6;   
const int btnMaxDownPin = 7; 
const int btnMinUpPin = 8;   
const int btnMinDownPin = 9; 

// --- CALIBRATION VARIABLES ---
int maxVolume = 3000;
int minVolume = 1000;
int noiseFloor = 150;  // The "error range" (0 to 150 is considered silence)
int stepSize = 100;    // How much the buttons change the values

// Button debounce timers to prevent double-clicking
unsigned long lastButtonPress = 0;
const int debounceDelay = 250; // Wait 250ms between button reads

void setup() {
  Serial.begin(115200); 

  // Initialize display
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }

  // Initialize motor PWM
  ledcAttach(vPin, 20000, 8);

  // Initialize Buttons
  pinMode(btnMaxUpPin, INPUT_PULLUP);
  pinMode(btnMaxDownPin, INPUT_PULLUP);
  pinMode(btnMinUpPin, INPUT_PULLUP);
  pinMode(btnMinDownPin, INPUT_PULLUP);

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("System Ready");
  display.display();
  delay(1000);
}

void loop() {
  // 1. READ BUTTONS TO ADJUST THRESHOLDS
  checkButtons();

  // 2. SAMPLE THE MICROPHONE
  int sampleWindow = 50; 
  unsigned long startMillis = millis(); 
  
  // FIX: Start Min at max possible, and Max at min possible!
  int signalMin = 4095; 
  int signalMax = 0;    
  
  while (millis() - startMillis < sampleWindow) {
    int sample = analogRead(micPin);
    if (sample < 4095) {
      if (sample > signalMax) { signalMax = sample; }
      if (sample < signalMin) { signalMin = sample; }
    }
  }
  
  int peakToPeak = signalMax - signalMin;
  
  // Print for debugging
  Serial.print("Volume Level: ");
  Serial.print(peakToPeak);
  Serial.print(" | Min: ");
  Serial.print(minVolume);
  Serial.print(" | Max: ");
  Serial.println(maxVolume);

  // 3. CHECK VOLUME AGAINST THRESHOLDS
  if (peakToPeak > maxVolume) {
    tooLoud();
  } 
  // Check if it's too quiet, BUT ensure it's above the noise floor
  else if (peakToPeak < minVolume && peakToPeak > noiseFloor) {
    tooQuiet();
  }
}

// --- BUTTON LOGIC & CONSTRAINTS ---
void checkButtons() {
  // Only check buttons if enough time has passed since the last press
  if (millis() - lastButtonPress > debounceDelay) {
    
    // INCREASE MAX
    if (digitalRead(btnMaxUpPin) == LOW) {
      // No constraint needed to go higher, but we can prevent it from exceeding 4095
      if (maxVolume + stepSize <= 4095) {
        maxVolume += stepSize;
        sendToDisplay("Maximum: " + String(maxVolume));
      }
      lastButtonPress = millis();
    }
    
    // DECREASE MAX
    else if (digitalRead(btnMaxDownPin) == LOW) {
      int newMax = maxVolume - stepSize;
      int vDifference = newMax - minVolume;
      
      // Constraint: Difference must be 500 or greater
      if (vDifference >= 500) {
        maxVolume = newMax;
        sendToDisplay("Maximum: " + String(maxVolume));
      } else {
        sendToDisplay("Limit Reached!"); // Optional warning
      }
      lastButtonPress = millis();
    }
    
    // INCREASE MIN
    else if (digitalRead(btnMinUpPin) == LOW) {
      int newMin = minVolume + stepSize;
      int vDifference = maxVolume - newMin;
      
      // Constraint: Difference must be 500 or greater
      if (vDifference >= 500) {
        minVolume = newMin;
        sendToDisplay("Minimum: " + String(minVolume));
      } else {
        sendToDisplay("Limit Reached!");
      }
      lastButtonPress = millis();
    }
    
    // DECREASE MIN
    else if (digitalRead(btnMinDownPin) == LOW) {
      // Make sure min volume doesn't drop into the noise floor
      if (minVolume - stepSize > noiseFloor) {
        minVolume -= stepSize;
        sendToDisplay("Minimum: " + String(minVolume));
      }
      lastButtonPress = millis();
    }
  }
}

// --- MOTOR BEHAVIORS ---
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

// --- DISPLAY HELPER ---
void sendToDisplay(String message) {
  display.clearDisplay();
  display.setCursor(0, 0);
  
  // Optional: Make font larger for these important alerts
  display.setTextSize(2); 
  display.println(message);
  display.display();
  
  // Reset text size for next time
  display.setTextSize(1); 
  
  Serial.print("Displaying: ");
  Serial.println(message);
}