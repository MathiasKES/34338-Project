#include <Servo.h>

constexpr uint8_t rPin   = 5;
constexpr uint8_t gPin = 4;
constexpr uint8_t buzzerPin = 2;
constexpr uint8_t servoPin  = 14;

void setup() {
  // put your setup code here, to run once:
  pinMode(rpin, OUTPUT);
  pinMode(gpin, OUTPUT);
  pinMode(buzzerPin, OUTPUT);
  myservo.attach(servoPin);


  digitalWrite(rPin, LOW);
  digitalWrite(gPin, LOW);
  digitalWrite(buzzerPin, LOW);  
  myservo.write(0);}

void loop() {
  // put your main code here, to run repeatedly:
  condition = true;

  if (condition) {
    Serial.println("Access Granted")
    digitalWrite(greenPin, HIGH);
    digitalWrite(redPin, LOW);

    // Buzzer OFF
    digitalWrite(buzzerPin, LOW);

    // Servo moves to open position
    myServo.write(180);
  } 
  else {
    // Red LED ON
    Serial.println("Wrong PIN")
    digitalWrite(redPin, HIGH);
    digitalWrite(greenPin, LOW);

    // Buzzer ON
    digitalWrite(buzzerPin, HIGH);

    // Servo closed
    myServo.write(0);
  }
  delay(50);
}
