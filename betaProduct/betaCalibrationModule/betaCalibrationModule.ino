#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Fonts/FreeSans9pt7b.h>

//  BOARD: Adafruit Feather ESP32-C6
//
// Pin numbers here are C6 GPIOs and do NOT carry to other boards — on an
// ESP32-WROOM-32D, IO7 sits on the internal SPI flash (won't boot) and IO5 is a
// strapping pin. If this ever moves boards, re-derive the whole map.
const int MIC_PIN   = A0;   // piezo vibration sensor
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
const float QUIET_FACTOR = 0.8;               // below speechAvg*this = too quiet
const float LOUD_FACTOR  = 1.3;               // above speechAvg*this = too loud
const unsigned long VOLUME_TRIGGER_MS = 1500; // must persist this long to fire

//  TUNABLES — alerts, display, calibration
const unsigned long ALERT_COOLDOWN_MS = 3000; // min gap between any two alerts
const unsigned long SILENCE_CAL_MS    = 5000; // length of the silent baseline
const unsigned long CALIBRATE_VOLUME_TIMEOUT_MS = 15000; // auto-finish if the user waits 15 s

const bool DEBUG_PLOT = true;  // true -> stream envelope + thresholds

//  CALIBRATION RESULTS (set once in setup)
int   micCenter = 0;       // resting ADC value of the mic (DC offset)
float noiseMeanSqRaw = 0;  // raw (un-gained) mean-square of room noise
float noiseRms  = 0;       // gained RMS of room noise (reporting only)
float speechAvg = 0;       // gained, noise-subtracted RMS of normal speech
float quietThreshold = 0;
float loudThreshold  = 0;
float windowMs       = 0;  // measured, not assumed — see measureWindowRate()

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
const int MENU_START_ITEM = 1;
const int MENU_CALIBRATE_ITEM = 2;
const int MENU_SELECT_OFFSET = 3;
const int MENU_HOME_OFFSET = 10;
const int MENU_SELECTED_START = MENU_START_ITEM + MENU_SELECT_OFFSET;
const int MENU_SELECTED_CALIBRATE = MENU_CALIBRATE_ITEM + MENU_SELECT_OFFSET;

enum MenuMode {
  MENU_NORMAL,
  MENU_SELECTED,
  MENU_ACTION
};

MenuMode menuMode = MENU_NORMAL;

int BUTTON_SELECTION = MENU_START_ITEM; // Default starting position on main menu: starts by having "start" selected
static const unsigned char PROGMEM image_arrow_right_bits[] = {0x08,0x04,0xfe,0x04,0x08}; // bitmap for arrow to appear
bool CHECK_VOLUME = false; // bool to turn on/off nudge
bool selectionActive = false;
bool listeningMode = false;
static bool prevUpState = false;
static bool prevDownState = false;
static bool prevSelectState = false;
static bool prevHomeState = false;
static unsigned long lastMenuEventMs = 0;
static unsigned long inputLockUntil = 0;
const unsigned long MENU_DEBOUNCE_MS = 80;
const unsigned long INPUT_LOCK_MS = 250;
const int BTN_CALIBRATE  = 6;   // in loop(): tests the "too loud" pattern
const int BTN_TEST_QUIET = 0;   // in loop(): tests the "too quiet" pattern
const int BTN_MENU_UP = 8; // FILLER INT
const int BTN_MENU_DOWN = 7; // FILLER INT
const int BTN_MENU_SELECT = 6; // FILLER INT
const int BTN_HOME = 9; // FILLER INT
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
  pinMode(BTN_MENU_UP,    INPUT_PULLUP);
  pinMode(BTN_MENU_DOWN,  INPUT_PULLUP);
  pinMode(BTN_MENU_SELECT, INPUT_PULLUP);
  pinMode(BTN_HOME,       INPUT_PULLUP);

  ledcAttach(MOTOR_PIN, MOTOR_PWM_FREQ, MOTOR_PWM_BITS);
  ledcWrite(MOTOR_PIN, 0);

  initDisplay();

  calibrateSilence();
  calibrateVolume();

  measureWindowRate();  // needs the calibration values, so it runs after them
  printCalibrationSummary();
  nudgeReady();
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
bool actionInProgress = false;

void mm_execution(int button_select) {
  unsigned long now = millis();

  if (actionInProgress) return;
  actionInProgress = true;

  switch (button_select) {
    case 1: {
      CHECK_VOLUME = true;
      listeningMode = true;
      selectionActive = false;
      inputLockUntil = millis() + INPUT_LOCK_MS;
      listeningScreen("LISTENING...", 10, 37);
      break;
    }
    case 2: {
      display.clearDisplay();
      calibrateSilence();
      calibrateVolume();
      nudgeReady();
      delay(3000);
      break;
    }
    default: { break; }
  }

  actionInProgress = false;
  selectionActive = false;
  BUTTON_SELECTION = MENU_START_ITEM;
  prevUpState = (digitalRead(BTN_MENU_UP) == LOW);
  prevDownState = (digitalRead(BTN_MENU_DOWN) == LOW);
  prevSelectState = (digitalRead(BTN_MENU_SELECT) == LOW);
  prevHomeState = (digitalRead(BTN_HOME) == LOW);
  lastMenuEventMs = millis();
  if (button_select != 1) {
    mm_selection(BUTTON_SELECTION);
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

  const bool upPressed = (digitalRead(BTN_MENU_UP) == LOW);
  const bool downPressed = (digitalRead(BTN_MENU_DOWN) == LOW);
  const bool selectPressed = (digitalRead(BTN_MENU_SELECT) == LOW);
  const bool homePressed = (digitalRead(BTN_HOME) == LOW);

  if (millis() < inputLockUntil) {
    prevUpState = upPressed;
    prevDownState = downPressed;
    prevSelectState = selectPressed;
    prevHomeState = homePressed;
    return;
  }

  if (listeningMode) {
    if (selectPressed && !prevSelectState) {
      listeningMode = false;
      CHECK_VOLUME = false;
      selectionActive = false;
      BUTTON_SELECTION = MENU_START_ITEM;
      inputLockUntil = millis() + INPUT_LOCK_MS;
      prevUpState = upPressed;
      prevDownState = downPressed;
      prevSelectState = selectPressed;
      prevHomeState = homePressed;
      lastMenuEventMs = millis();
      mm_selection(BUTTON_SELECTION);
      return;
    }

    prevUpState = upPressed;
    prevDownState = downPressed;
    prevSelectState = selectPressed;
    prevHomeState = homePressed;
    return;
  }

  if (actionInProgress) {
    prevUpState = upPressed;
    prevDownState = downPressed;
    prevSelectState = selectPressed;
    prevHomeState = homePressed;
    return;
  }

  // Release the selected item immediately, even if it happened very quickly
  // after the press edge. That prevents a short tap from leaving the menu in a
  // half-selected state and requiring a second button press.
  if (!selectPressed && prevSelectState && BUTTON_SELECTION > 3) {
    const int actionSelection = BUTTON_SELECTION - MENU_SELECT_OFFSET;
    BUTTON_SELECTION = actionSelection;
    selectionActive = false;
    prevUpState = upPressed;
    prevDownState = downPressed;
    prevSelectState = selectPressed;
    prevHomeState = homePressed;
    lastMenuEventMs = millis();
    mm_execution(BUTTON_SELECTION);
    return;
  }

  if ((millis() - lastMenuEventMs) < MENU_DEBOUNCE_MS) {
    prevUpState = upPressed;
    prevDownState = downPressed;
    prevSelectState = selectPressed;
    prevHomeState = homePressed;
    return;
  }

  // Ignore all non-home input while an action is running.
  if (actionInProgress) {
    return;
  }

  // Only move the highlight while the menu is in its normal unselected state.
  if (!selectionActive) {
    if (upPressed && !prevUpState) {
      if (BUTTON_SELECTION == MENU_START_ITEM) {
        Serial.println("Top element already selected!");
      } else if (BUTTON_SELECTION == MENU_CALIBRATE_ITEM) {
        BUTTON_SELECTION = MENU_START_ITEM;
        mm_selection(BUTTON_SELECTION);
        lastMenuEventMs = millis();
      }
    }

    if (downPressed && !prevDownState) {
      if (BUTTON_SELECTION == MENU_CALIBRATE_ITEM) {
        Serial.println("Bottom element already selected!");
      } else if (BUTTON_SELECTION == MENU_START_ITEM) {
        BUTTON_SELECTION = MENU_CALIBRATE_ITEM;
        mm_selection(BUTTON_SELECTION);
        lastMenuEventMs = millis();
      }
    }
  }

  // Select: press highlights, release executes the highlighted item.
  if (selectPressed && !prevSelectState && BUTTON_SELECTION < 3) {
    BUTTON_SELECTION = BUTTON_SELECTION + MENU_SELECT_OFFSET;
    selectionActive = true;
    mm_selection(BUTTON_SELECTION);
    lastMenuEventMs = millis();
  }

  prevUpState = upPressed;
  prevDownState = downPressed;
  prevSelectState = selectPressed;
  prevHomeState = homePressed;
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
    return;
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
  showMessage("Cal 2/2\n\nRead aloud:\n\n'The quick brown\nfox jumps over\nthe lazy dog'\nthen press select", 1);

  double rmsSum = 0;
  long windows = 0;
  unsigned long start = millis();
  bool buttonPressed = false;

  // Keep sampling until the user presses the button or the timeout expires.
  while (!buttonPressed && (millis() - start) < CALIBRATE_VOLUME_TIMEOUT_MS) {
    buttonPressed = (digitalRead(BTN_CALIBRATE) == LOW);
    rmsSum += readRmsWindow();
    windows++;
  }

  if (buttonPressed) {
    waitForButtonRelease();
  } else {
    Serial.println("No input detected; finishing calibration automatically after 15 seconds.");
    delay(500);
  }

  if (windows == 0) {
    rmsSum += readRmsWindow();
    windows = 1;
  }

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

// How long one RMS window actually takes. It matters because the envelope's
// time constant is windowMs / ENV_ALPHA — so analogRead() speed silently sets
// how fast the whole detector reacts, and the silence floor's pause behaviour
// depends on it. Expect ~7 ms here, giving ~0.35 s. Worth a glance after any
// change to WINDOW_SIZE, the sensor, or the board.
void measureWindowRate() {
  unsigned long t0 = micros();
  for (int i = 0; i < 20; i++) readRmsWindow();
  windowMs = (micros() - t0) / 20000.0;  // 20 windows, µs -> ms
}

void printCalibrationSummary() {
  Serial.println("\n===== CALIBRATION SUMMARY =====");
  Serial.print("mic center:      "); Serial.println(micCenter);
  Serial.print("noise RMS:       "); Serial.println(noiseRms);
  Serial.print("speech avg:      "); Serial.println(speechAvg);
  Serial.print("quiet threshold: "); Serial.println(quietThreshold);
  Serial.print("loud threshold:  "); Serial.println(loudThreshold);
  Serial.print("window:          "); Serial.print(windowMs);   Serial.println(" ms");
  Serial.print("env time const:  "); Serial.print(windowMs / ENV_ALPHA / 1000.0);
  Serial.println(" s   (was ~0.35 s on the C6)");
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