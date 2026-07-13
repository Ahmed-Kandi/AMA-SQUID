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
  } else if (digitalRead(tooQ) == 0) {
    tooQuiet();
  }
}

void tooQuiet() {

}

void tooLoud() {

}