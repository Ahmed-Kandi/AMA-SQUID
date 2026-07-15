#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Fonts/FreeSans9pt7b.h>

const int MIC_PIN   = A0;   // now the piezo bone-conduction sensor, not the KY-037
const int MOTOR_PIN = 5;

const int MOTOR_PWM_FREQ = 20000;
const int MOTOR_PWM_BITS = 8;

const int OLED_ADDR = 0x3C;
Adafruit_SSD1306 display(128, 64, &Wire, -1);

const int WINDOW_SIZE = 100;

// GAIN was tuned for the KY-037's tiny RMS range (speech ~6, silence ~2.5).
// The piezo sensor will almost certainly have very different signal levels —
// recalibration handles the thresholds automatically, but if speechAvg comes
// out extremely large or tiny in the calibration summary, revisit this.
const float GAIN = 5.0;

// EMA smoothing: env = (1-alpha)*env + alpha*newSample.
// Higher alpha = reacts faster but jitters more.
// One RMS window is ~7 ms, so 0.02 gives a ~0.4 s time constant: env rides
// over the gaps between words and tracks sentence-level loudness — the same
// mix of speech + gaps the volume calibration averages, so the two are in
// matching units.
const float ENV_ALPHA = 0.02;

//  TUNABLES — volume detection
const float QUIET_FACTOR = 0.6;               // below speechAvg*0.6 = too quiet
const float LOUD_FACTOR  = 1.4;               // above speechAvg*1.4 = too loud
const unsigned long VOLUME_TRIGGER_MS = 1500; // must persist this long to fire

//  TUNABLES — alerts, display, calibration
const unsigned long ALERT_COOLDOWN_MS = 3000; // min gap between any two alerts
const unsigned long SILENCE_CAL_MS    = 5000; // length of the silent baseline

const bool DEBUG_PLOT = false;  // true -> stream envelope + thresholds

//  CALIBRATION RESULTS (set once in setup)
int   micCenter = 0;       // resting ADC value of the mic (DC offset)
float noiseMeanSqRaw = 0;  // raw (un-gained) mean-square of room noise
float noiseRms  = 0;       // gained RMS of room noise (reporting only)
float speechAvg = 0;       // gained, noise-subtracted RMS of normal speech
float quietThreshold = 0;
float loudThreshold  = 0;

//  RUNTIME STATE
float env = 0;             // smoothed volume envelope
// millis() timestamps; 0 means "condition not currently active"
unsigned long quietStart = 0;
unsigned long loudStart  = 0;
// alert pacing
unsigned long lastAlertMs        = 0;
unsigned long volumeBlockedUntil = 0;
const int ALERT_LOUD  = 0;
const int ALERT_QUIET = 1;

// screen integers and interaction integers
int BUTTON_SELECTION = 1; // Default starting position on main menu: starts by having "start" selected
static const unsigned char PROGMEM image_arrow_right_bits[] = {0x08,0x04,0xfe,0x04,0x08}; // bitmap for arrow to appear
bool CHECK_VOLUME = false; // bool to turn on/off nudge
const int BTN_CALIBRATE  = 6;   // in loop(): tests the "too loud" pattern
const int BTN_TEST_QUIET = 7;   // in loop(): tests the "too quiet" pattern
const int BTN_MENU_UP = 0; // FILLER INT
const int BTN_MENU_DOWN = 0; // FILLER INT
const int BTN_MENU_SELECT = 0; // FILLER INT
const int BTN_HOME = 0; // FILLER INT
const int BTN_VOLUME_UP = 0; // FILLER INT
const int BTN_VOLUME_DOWN = 0; // FILLER INT

// ===========================================================================
//  DISPLAY SCREENS
// ===========================================================================

void startButton() {

  display.clearDisplay();

  // Layer 1
  display.drawRect(0, 0, 128, 64, 1);

  // Layer 2
  display.setTextColor(1);
  display.setTextWrap(false);
  display.setCursor(53, 29);
  display.print("");

  // Layer 4
  display.setFont(&FreeSans9pt7b);
  display.setCursor(12, 17);
  display.print("THE NUDGE");

  // Layer 4
  display.fillRect(40, 27, 56, 9, 1);

  // Layer 5
  display.setTextColor(0);
  display.setFont();
  display.setCursor(54, 28);
  display.print("START");

  // Layer 4 copy 1
  display.fillRect(36, 39, 56, 9, 1);

  // Layer 5 copy 1
  display.setCursor(38, 40);
  display.print("CALIBRATE");

  // arrow_right
  display.drawBitmap(30, 29, image_arrow_right_bits, 7, 5, 1);

  display.display();

}

void startButtonSelected(){

  display.clearDisplay();

  // Layer 1
  display.drawRect(0, 0, 128, 64, 1);

  // Layer 2
  display.setTextColor(1);
  display.setTextWrap(false);
  display.setCursor(53, 29);
  display.print("");

  // Layer 4
  display.setFont(&FreeSans9pt7b);
  display.setCursor(12, 17);
  display.print("THE NUDGE");

  // Layer 4
  display.drawRect(40, 27, 56, 9, 1);

  // Layer 5
  display.setFont();
  display.setCursor(54, 28);
  display.print("START");

  // Layer 4 copy 1
  display.fillRect(36, 39, 56, 9, 1);

  // Layer 5 copy 1
  display.setTextColor(0);
  display.setCursor(38, 40);
  display.print("CALIBRATE");

  // arrow_right
  display.drawBitmap(30, 29, image_arrow_right_bits, 7, 5, 1);

  display.display();

}

void calibrateButton(){
  
  display.clearDisplay();

  // Layer 1
  display.drawRect(0, 0, 128, 64, 1);

  // Layer 2
  display.setTextColor(1);
  display.setTextWrap(false);
  display.setCursor(53, 29);
  display.print("");

  // Layer 4
  display.setFont(&FreeSans9pt7b);
  display.setCursor(12, 17);
  display.print("THE NUDGE");

  // Layer 4
  display.fillRect(36, 27, 56, 9, 1);

  // Layer 5
  display.setTextColor(0);
  display.setFont();
  display.setCursor(50, 28);
  display.print("START");

  // Layer 4 copy 1
  display.fillRect(40, 39, 56, 9, 1);

  // Layer 5 copy 1
  display.setCursor(42, 40);
  display.print("CALIBRATE");

  // arrow_right
  display.drawBitmap(30, 41, image_arrow_right_bits, 7, 5, 1);

  display.display();

}

void calibrateButtonSelected(){

  display.clearDisplay();

  // Layer 1
  display.drawRect(0, 0, 128, 64, 1);

  // Layer 2
  display.setTextColor(1);
  display.setTextWrap(false);
  display.setCursor(53, 29);
  display.print("");

  // Layer 4
  display.setFont(&FreeSans9pt7b);
  display.setCursor(12, 17);
  display.print("THE NUDGE");

  // Layer 4
  display.fillRect(36, 27, 56, 9, 1);

  // Layer 5
  display.setTextColor(0);
  display.setFont();
  display.setCursor(50, 28);
  display.print("START");

  // Layer 4 copy 1
  display.drawRect(40, 39, 56, 9, 1);

  // Layer 5 copy 1
  display.setTextColor(1);
  display.setCursor(42, 40);
  display.print("CALIBRATE");

  // arrow_right
  display.drawBitmap(30, 41, image_arrow_right_bits, 7, 5, 1);

  display.display();

}

void listeningScreen(String volumeMessage, int x, int y) {

  display.clearDisplay();
  display.drawRect(0, 0, 128, 64, 1);

  display.setTextColor(1);
  display.setTextWrap(false);
  display.setCursor(53, 29);
  display.print("");

  display.setFont(&FreeSans9pt7b);
  display.setCursor(x, y);
  display.print(volumeMessage);

  display.display();

}

void nudgeReady() {

  display.clearDisplay();
  display.drawRect(0, 0, 128, 64, 1);

  display.setTextColor(1);
  display.setTextWrap(false);
  display.setCursor(53, 29);
  display.print("");

  display.setFont(&FreeSans9pt7b);
  display.setCursor(31, 27);
  display.print("NUDGE");

  display.setCursor(30, 47);
  display.print("READY!");

  display.display();

}

//  SETUP
void setup() {

  Serial.begin(115200);
  delay(1000);

  pinMode(BTN_CALIBRATE,  INPUT_PULLUP);

  ledcAttach(MOTOR_PIN, MOTOR_PWM_FREQ, MOTOR_PWM_BITS);
  ledcWrite(MOTOR_PIN, 0);

  initDisplay();

  calibrateSilence();
  calibrateVolume();

  printCalibrationSummary();
  showMessage("Nudge\nready!", 2);
  delay(3000);
  mm_selection(BUTTON_SELECTION);

  // Seed the envelope at normal-speech level so we don't get an instant
  // "too quiet" while it ramps up from zero, and give a short grace period.
  env = speechAvg;
  volumeBlockedUntil = millis() + 2000;

}

// ===========================================================================
//  SELECTION AND EXECUTION FUNCTIONS
// ===========================================================================

void mm_selection(int button_select) {
  // Button selection is assigned to different integers
  const int start_button = 1;
  const int calibrate_button = 2;
  const int start_selected = 4;
  const int calibrate_selected = 5;

  // Switch case for changing the display when the integer value changes
  switch (button_select) {
    case start_button: { display.clearDisplay(); startButton(); break; }
    case calibrate_button: { display.clearDisplay(); calibrateButton(); break; }
    case start_selected: { display.clearDisplay(); startButtonSelected(); break; }
    case calibrate_selected: { display.clearDisplay(); calibrateButtonSelected(); break; }  
    default: break;
  }
}

// Actually executes the command of the button we have selected
void mm_execution(int button_select) {
  unsigned long now = millis();

  switch (button_select) {
    case 1: { display.clearDisplay(); checkVolume(now, CHECK_VOLUME = true); break; } // "START" button execution
    case 2: { display.clearDisplay(); calibrateSilence(); calibrateVolume(); break; } // "CALIBRATE" button execution
    // case button_select == 3: { display.ClearDisplay(); } // "ADJUST" button execution
    default: { break; }
  }
}

//  MAIN LOOP
void loop() {
  unsigned long now = millis();

  // --- measure one window and update the envelope ---
  float rms = readRmsWindow();
  env = (1.0 - ENV_ALPHA) * env + ENV_ALPHA * rms;

  if (DEBUG_PLOT) {
    // Open Tools > Serial Plotter to watch these while tuning. Printed only
    // every 4th window — at full loop speed the serial port can't keep up
    // and the blocked writes would distort the sample timing.
    static int plotDivider = 0;
    if (++plotDivider >= 4) {
      plotDivider = 0;
      Serial.print("env:");       Serial.print(env);
      Serial.print(" quietThr:"); Serial.print(quietThreshold);
      Serial.print(" loudThr:");  Serial.println(loudThreshold);
    }
  }

  checkVolume(now, CHECK_VOLUME);

  if (digitalRead(BTN_MENU_UP) == HIGH && BUTTON_SELECTION == 1) {
    Serial.println("Top element already selected!");
  } else if (digitalRead(BTN_MENU_UP) == HIGH && BUTTON_SELECTION != 1) {
    BUTTON_SELECTION = BUTTON_SELECTION - 1;
    mm_selection(BUTTON_SELECTION);
    delay(150);
  }

  if (digitalRead(BTN_MENU_DOWN) == HIGH && BUTTON_SELECTION == 2) {
    Serial.println("Bottom element already selected!");
  } else if (digitalRead(BTN_MENU_DOWN) == HIGH && BUTTON_SELECTION != 2) {
    BUTTON_SELECTION = BUTTON_SELECTION + 1;
    mm_selection(BUTTON_SELECTION); // +1 is to simply move arrow on display for selection
    delay(150); 
  }

  if (digitalRead(BTN_MENU_SELECT) == HIGH && BUTTON_SELECTION > 3) {
    Serial.println("Holding down select button!");
    delay(250); // makes sure not to keep adding onto the selection if integer is bigger than 3 (meaning its in selection mode)
  } else if (digitalRead(BTN_MENU_SELECT) == HIGH && BUTTON_SELECTION <= 3) {
    BUTTON_SELECTION = BUTTON_SELECTION + 3;
    mm_selection(BUTTON_SELECTION); // +3 is for darkening the box to indicate seletion is occuring
    delay(150); 
  }
  
  if (BUTTON_SELECTION > 3 && digitalRead(BTN_MENU_SELECT) == LOW) {
    BUTTON_SELECTION = BUTTON_SELECTION - 3;
    delay(150);
    mm_execution(BUTTON_SELECTION); // Actually executes what we have selected on the main menu (i.e; "Start" would start up the nudge process)
  }

  if (digitalRead(BTN_HOME) == HIGH && BUTTON_SELECTION > 10) { // Checks if button is getting reading, then checks if BUTTON_SELECTION is bigger than 10, indicating it is being held down
    Serial.println("Holding down home button!");
    delay(150);
  } else if (digitalRead(BTN_HOME) == HIGH && BUTTON_SELECTION < 10) { // 
    BUTTON_SELECTION = BUTTON_SELECTION + 10;
  } else if (digitalRead(BTN_HOME) == LOW && BUTTON_SELECTION > 10) {
    delay(150);
    BUTTON_SELECTION = 1;
    CHECK_VOLUME = false;
    checkVolume(now, CHECK_VOLUME);
    mm_selection(BUTTON_SELECTION);
  }

}

//  SIGNAL MEASUREMENT

// One RMS window: WINDOW_SIZE reads, deviation from the calibrated center,
// NOISE-SUBTRACTED root-mean-square, then software gain. Used by calibration
// AND detection so both always speak the same units.
// The noise subtraction is the key to the whole sketch: mic RMS measures
// speech power PLUS room-noise power, so without it the envelope can never
// fall below the noise floor — "too quiet" becomes unreachable. Powers add,
// so we subtract the calibrated noise mean-square BEFORE the square root:
// what's left is (an estimate of) the speech-only level. Silence now reads ~0.
float readRmsWindow() {
  long sumSq = 0;
  for (int i = 0; i < WINDOW_SIZE; i++) {
    long deviation = analogRead(MIC_PIN) - micCenter;
    sumSq += deviation * deviation;
  }
  float meanSq = (float)sumSq / WINDOW_SIZE - noiseMeanSqRaw;
  if (meanSq < 0) meanSq = 0;  // noise fluctuates; don't sqrt a negative
  return GAIN * sqrt(meanSq);
}

//  DETECTION CHECKS

void checkVolume(unsigned long now, bool isChecking) {
  if (!isChecking) {
    Serial.println("Cannot check volume right now!");
  }
  else if (isChecking) {
    if (now < volumeBlockedUntil) return;

    // TOO QUIET: envelope must stay below threshold for the full trigger
    // duration — short dips (pauses between words) reset nothing because the
    // timer only clears when volume returns to the normal band.
    if (env < quietThreshold) {
      if (quietStart == 0) {
        quietStart = now;
      } else if (now - quietStart > VOLUME_TRIGGER_MS) {
        fireAlert(ALERT_QUIET);
      }
    } else {
      quietStart = 0;
    }

    // TOO LOUD: same pattern, other direction.
    if (env > loudThreshold) {
      if (loudStart == 0) {
        loudStart = now;
      } else if (now - loudStart > VOLUME_TRIGGER_MS) {
        fireAlert(ALERT_LOUD);
      }
    } else {
      loudStart = 0;
    }
  }
}

//  ALERTS

// Shows the message, runs the haptic pattern (blocking — the mic is not
// sampled while the motor pattern plays), then resets detection state so we
// re-measure from scratch instead of instantly re-firing on stale data.
void fireAlert(int kind) {
  unsigned long now = millis();
  if (now - lastAlertMs < ALERT_COOLDOWN_MS && lastAlertMs != 0) return;

  switch (kind) {
    case ALERT_LOUD:  listeningScreen("TOO LOUD!", 14, 37);  patternTooLoud();  break;
    case ALERT_QUIET: listeningScreen("TOO QUIET!", 12, 37); patternTooQuiet(); break;
  }

  resetDetectionState();
}

void resetDetectionState() {
  unsigned long now = millis();
  quietStart = 0;
  loudStart  = 0;
  lastAlertMs = now;
  volumeBlockedUntil = now + ALERT_COOLDOWN_MS;
  listeningScreen("LISTENING...", 10, 37);
}

//  HAPTIC PATTERNS

// Smooth ramp DOWN in intensity: "bring it down".
void patternTooLoud() {
  for (int i = 0; i < 3; i++) {
    ledcWrite(MOTOR_PIN, 255);
    delay(100);
    ledcWrite(MOTOR_PIN, 0);
    delay(75);
  }
  for (int i = 100; i <= 500; i += 100) {
    ledcWrite(MOTOR_PIN, 255);
    delay(100);
    ledcWrite(MOTOR_PIN, 0);
    delay(i);
  }
}

// Smooth ramp UP in intensity: "bring it up".
void patternTooQuiet() {
  for (int i = 32; i < 255; i += 8) {
    ledcWrite(MOTOR_PIN, i);
    delay(50);
  }
  ledcWrite(MOTOR_PIN, 0);
}



//  CALIBRATION

// Step 1: 5 s of silence. One pass computes both the mean (the mic's resting
// center / DC offset) and the variance — the raw mean-square power of room
// noise, which readRmsWindow() subtracts from every window from now on.
void calibrateSilence() {
  Serial.println("\n[1/2] Calibrating baseline — please stay quiet!");
  showMessage("Cal 1/2\n\nStay\nquiet...", 1);

  unsigned long long sum   = 0;  // 64-bit: 5 s of 12-bit reads overflows 32
  unsigned long long sumSq = 0;
  unsigned long n = 0;
  unsigned long start = millis();
  unsigned long lastDot = start;

  while (millis() - start < SILENCE_CAL_MS) {
    unsigned long long v = analogRead(MIC_PIN);
    sum   += v;
    sumSq += v * v;
    n++;
    if (millis() - lastDot >= 1000) {  // one progress dot per second
      Serial.print(".");
      lastDot += 1000;
    }
  }
  Serial.println();

  float mean   = (float)sum / n;
  float meanSq = (float)sumSq / n;
  micCenter = (int)mean;
  // variance = E[x^2] - (E[x])^2 — the raw mean-square power of room noise.
  float variance = meanSq - mean * mean;
  if (variance < 0) variance = 0;  // guard against float rounding
  noiseMeanSqRaw = variance;
  noiseRms = GAIN * sqrt(variance);  // kept for the summary printout

  Serial.print("  mic center = "); Serial.println(micCenter);
  Serial.print("  noise RMS (gained) = "); Serial.println(noiseRms);
  delay(1000);
}

// Step 2: normal speaking volume. The user reads a sentence out loud and
// presses the button when done. We average RMS windows measured with the
// exact same readRmsWindow() used at runtime, so the threshold factors are
// applied to like-for-like numbers.
void calibrateVolume() {
  Serial.println("\n[2/2] Calibrating normal speech volume.");
  Serial.println("Read this aloud at your normal volume, then press the button:");
  Serial.println("-------------------------------------------");
  Serial.println("The quick brown fox jumps over the lazy dog");
  Serial.println("-------------------------------------------");
  showMessage("Cal 2/2\n\nRead aloud:\n\n'The quick brown\nfox jumps over\nthe lazy dog'\nthen press button", 1);

  double rmsSum = 0;
  long windows = 0;
  // Keep sampling until the button is pressed, but insist on at least one
  // window so an accidental early press can't divide by zero.
  while (digitalRead(BTN_CALIBRATE) == HIGH || windows == 0) {
    rmsSum += readRmsWindow();
    windows++;
  }
  waitForButtonRelease();

  speechAvg = rmsSum / windows;
  // With noise subtracted, silence sits near 0, so plain fractions of the
  // speech average are meaningful thresholds.
  quietThreshold = speechAvg * QUIET_FACTOR;
  loudThreshold  = speechAvg * LOUD_FACTOR;

  Serial.print("  speech avg (gained RMS) = "); Serial.println(speechAvg);
  delay(500);
}

void waitForButtonRelease() {
  while (digitalRead(BTN_CALIBRATE) == LOW) { delay(10); }
  delay(250);
}

void printCalibrationSummary() {
  Serial.println("\n===== CALIBRATION SUMMARY =====");
  Serial.print("mic center:      "); Serial.println(micCenter);
  Serial.print("noise RMS:       "); Serial.println(noiseRms);
  Serial.print("speech avg:      "); Serial.println(speechAvg);
  Serial.print("quiet threshold: "); Serial.println(quietThreshold);
  Serial.print("loud threshold:  "); Serial.println(loudThreshold);
  Serial.println("===============================\n");
}

//  DISPLAY

void initDisplay() {
  display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.display();
}

void showMessage(const String &msg, int textSize) {
  Serial.print("Display: ");
  Serial.println(msg);

  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(textSize);
  display.println(msg);
  display.display();
  display.setTextSize(1);
}
