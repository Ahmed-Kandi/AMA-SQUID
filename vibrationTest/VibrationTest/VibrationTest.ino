int vPin = 5;
int tooLoudPin = 6;
int tooQuietPin = 7;

void setup() {
  // put your setup code here, to run once:
  ledcAttach(vPin, 20000, 8);
  pinMode(tooLoudPin, INPUT_PULLUP);
  pinMode(tooQuietPin, INPUT_PULLUP);
  Serial.begin(9600);
}

void loop() {
  Serial.print(digitalRead(tooLoudPin));
  Serial.print(" ");
  Serial.println(digitalRead(tooQuietPin));
  delay(10);
  if (digitalRead(tooLoudPin) == 0) {
    tooLoud();
  } else if (digitalRead(tooQuietPin) == 0) {
    tooQuiet();
  }
}

void tooQuiet() {
  for (int i = 32; i < 255; i += 8) {
    ledcWrite(vPin, i);
    delay(50);
  }
  ledcWrite(vPin, 0);
}

void tooLoud() {
  for (int i = 255; i > 32; i -= 8) {
    ledcWrite(vPin, i);
    delay(50);
  }
  ledcWrite(vPin, 0);
}