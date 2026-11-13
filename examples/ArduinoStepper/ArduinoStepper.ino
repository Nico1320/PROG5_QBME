#include <Prog5Climate.h>

// UNO Q D2 -> STEP, D3 -> DIR; external 3.3 V-compatible driver and motor supply.
// Driver unspecified: check timing, current limit, enable and grounding.
// Motor coils and motor power must not connect to GPIO.
prog5::ArduinoStepperIo motorPins(2, 3);
// Nominal 250 pulses/s plus overhead; no acceleration. Check starting load.
prog5::Stepper motor(motorPins, 2000);

void setup() {
    Serial.begin(115200);
    motor.begin();
}

void loop() {
    motor.move(200); // commanded pulses, not necessarily one physical revolution
    delay(1000);
    motor.move(-200);
    delay(1000);
}
