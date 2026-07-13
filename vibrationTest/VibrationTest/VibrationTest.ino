int vPin = 5;
int tooLoudPin = 6;
int tooQuietPin = 7;

void setup() {
  // put your setup code here, to run once:
  ledcAttach(vPin, 20000, 8);
  pinMode(tooLoudPin, INPUT);
  pinMode(tooQuietPin, INPUT);
  digitalWrite(tooLoudPin, HIGH);
  digitalWrite(tooQuietPin, HIGH);
}

void loop() {
  // put your main code here, to run repeatedly:
  if (digitalRead(tooLoudPin) == 0) {
    tooLoud();
  } else if (digitalRead(tooQuietPin) == 0) {
    tooQuiet();
  }
}

void tooQuiet() {
  
}

void tooLoud() {
  for (int i = 0; i < 255; i++) {
    ledcWrite(vPin, i);
    delay(10);
    ledcWrite(vPin, 0);
    delay(10);
  }
}