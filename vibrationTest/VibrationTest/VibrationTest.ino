int vPin = 5;
int tooLoudPin = 6;
int tooQuietPin = 7;
int tooFastPin = 8;
int tooSlowPin = 9;

void setup() {
  // put your setup code here, to run once:
  ledcAttach(vPin, 20000, 8);
  pinMode(tooLoudPin, INPUT_PULLUP);
  pinMode(tooQuietPin, INPUT_PULLUP);
  pinMode(tooFastPin, INPUT_PULLUP);
  pinMode(tooSlowPin, INPUT_PULLUP);
  Serial.begin(9600);
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
  for (int i = 255; i > 32; i -= 8) {
    ledcWrite(vPin, i);
    delay(50);
  }
  ledcWrite(vPin, 0);
}

void tooQuiet() {
  for (int i = 32; i < 255; i += 8) {
    ledcWrite(vPin, i);
    delay(50);
  }
  ledcWrite(vPin, 0);
}

void tooFast() {
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