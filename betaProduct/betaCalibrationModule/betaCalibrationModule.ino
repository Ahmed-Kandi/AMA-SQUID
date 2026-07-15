#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

const int MIC_PIN   = A0;   // now the piezo bone-conduction sensor, not the KY-037
const int MOTOR_PIN = 5;

const int BTN_CALIBRATE = 7;

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

  // Seed the envelope at normal-speech level so we don't get an instant
  // "too quiet" while it ramps up from zero, and give a short grace period.
  env = speechAvg;
  volumeBlockedUntil = millis() + 2000;
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

  checkVolume(now);
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

void checkVolume(unsigned long now) {
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



//  ALERTS

// Shows the message, runs the haptic pattern (blocking — the mic is not
// sampled while the motor pattern plays), then resets detection state so we
// re-measure from scratch instead of instantly re-firing on stale data.
void fireAlert(int kind) {
  unsigned long now = millis();
  if (now - lastAlertMs < ALERT_COOLDOWN_MS && lastAlertMs != 0) return;

  switch (kind) {
    case ALERT_LOUD:  showMessage("Too\nLoud!", 2);  patternTooLoud();  break;
    case ALERT_QUIET: showMessage("Too\nQuiet!", 2); patternTooQuiet(); break;
  }

  resetDetectionState();
}

void resetDetectionState() {
  unsigned long now = millis();
  quietStart = 0;
  loudStart  = 0;
  lastAlertMs = now;
  volumeBlockedUntil = now + ALERT_COOLDOWN_MS;
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
