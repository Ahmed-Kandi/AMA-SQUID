int button1 = 6;

int micPin = A0;
int baseline;
int average;
int windowSize = 100;
float smoothed;
float quietThreshold;
float loudThreshold;

unsigned long quietStart = 0;
unsigned long loudStart = 0;
int triggerDuration = 1500;   // ms it must persist before firing

void setup() {
  Serial.begin(9600);
  pinMode(button1, INPUT_PULLUP);

  unsigned long startTime = millis();
  unsigned long sum = 0;
  int count = 0;
  delay(1000);

  //Baseline
  Serial.println("\nCalibrating Baseline (Please stay quiet!)");
  while (millis() - startTime < 5000) {
    sum += analogRead(micPin);
    count++;
    if (millis() % 1000 == 0) {
      Serial.print(".");
    }
  }
  Serial.println();
  baseline = sum / count;
  Serial.println(baseline);
  delay(1000);

  //Normal Speaking Level
  sum = 0;
  count = 0;

  Serial.println("\nCalibrating Normal Speech (Please read this aloud, then press the button when you're done!)");
  Serial.println("-------------------------------------------\nThe quick brown fox jumps over the lazy dog\n-------------------------------------------");
  while (digitalRead(button1) != 0) {
    sum += abs(analogRead(micPin) - baseline);
    count++;
  }
  Serial.println();
  average = sum / count;
  quietThreshold = average * 0.8;   // below this = too quiet
  loudThreshold  = average * 1.5;

  Serial.println(average);
  delay(5000);
}

void loop() {
  long sumOfSquares = 0;

  for (int i = 0; i < windowSize; i++) {
    int deviation = analogRead(micPin) - baseline;
    sumOfSquares += (long)deviation * deviation;   // cast to long before squaring
  }

  float rms = sqrt((float)sumOfSquares / windowSize);
  rms *= 5;
  smoothed = 0.7 * smoothed + 0.3 * rms;
  // Serial.println(smoothed);



  // TOO QUIET check
  if (smoothed < quietThreshold) {
    if (quietStart == 0) {
      quietStart = millis();            // start the timer
    } else if (millis() - quietStart > triggerDuration) {
      Serial.println("TOO QUIET");
      quietStart = millis();            // reset so it doesn't spam every loop
    }
  } else {
    quietStart = 0;                     // back to normal, reset timer
  }

  // TOO LOUD check
  if (smoothed > loudThreshold) {
    if (loudStart == 0) {
      loudStart = millis();
    } else if (millis() - loudStart > triggerDuration) {
      Serial.println("TOO LOUD");
      loudStart = millis();
    }
  } else {
    loudStart = 0;
  }

  // Serial.print("smoothed: ");
  // Serial.print(smoothed);
  // Serial.print("  average: ");
  // Serial.print(average);
  // Serial.print("  quietThresh: ");
  // Serial.println(average * 0.6);
}