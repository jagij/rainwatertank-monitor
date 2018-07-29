#include <ArduinoSort.h>

/*
* Ultrasonic Sensor HC-SR04 and Arduino Tutorial
*
* by Dejan Nedelkovski,
* www.HowToMechatronics.com
*
*/

// defines pins numbers
const int trigPin = 4;
const int echoPin = 5;

// delay between two measurements, increase if too much echos
const int measureDelayMs = 60; // minimal 60ms per docs HC-SR04
const int measurementsForModus = 100;
const int groupNumMeasurements = 500;
const int cutoffDistanceMm = 50;

// defines variables
int estimateMean = -1;

void setup() {
  pinMode(trigPin, OUTPUT); // Sets the trigPin as an Output
  pinMode(echoPin, INPUT); // Sets the echoPin as an Input
  Serial.begin(9600); // Starts the serial communication
}

void loop() {
  if (estimateMean == -1) {
    estimateMean = getModus();
    Serial.print("Modus:");
    Serial.println(estimateMean);
  }
  int numMeasurements = 0;
  int sum = 0;
  for (int i = 0; i != groupNumMeasurements; ++i) {
    int measurementMm = getDistanceMm();
    if (measurementMm < estimateMean - cutoffDistanceMm) {
      continue;
    }
    if (measurementMm > estimateMean + cutoffDistanceMm) {
      continue;
    }
    numMeasurements++;
    sum += measurementMm;
  }
  if (numMeasurements == 0) {
    return;
  }
  estimateMean = sum/numMeasurements;
  Serial.print("Avg distance: ");
  Serial.println(estimateMean);
}

int getModus() {
  Serial.println("Getting modus");
  // read measurementsForModus values, calculate the modus
  int distances[measurementsForModus];
  for (int i = 0; i != measurementsForModus; ++i) {
    distances[i] = getDistanceMm();
  }
  sortArray(distances, measurementsForModus);
  int maxCount = 0;
  int maxValue = distances[0];
  int currentValue = maxValue;
  int currentCount = 0;
  for (int i = 0; i != measurementsForModus; ++i) {
    if (currentValue != distances[i]) {
      if (currentCount > maxCount) {
        maxCount = currentCount;
        maxValue = currentValue;
      }
      currentValue = distances[i];
      currentCount = 0;
    }
    currentCount++;
  }
  if (currentCount > maxCount) {
    maxCount = currentCount;
    maxValue = currentValue;
  }
  return maxValue;
}

int getDistanceMm() {
  long duration;
  int distance;
  // Clears the trigPin
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  // Sets the trigPin on HIGH state for 10 micro seconds
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  // Reads the echoPin, returns the sound wave travel time in microseconds
  duration = pulseIn(echoPin, HIGH);
  // Calculating the distance
  distance = duration*0.034/2*10;
  // Prints the distance on the Serial Monitor
//  Serial.print("Distance: ");
//  Serial.println(distance);
  delayMicroseconds(measureDelayMs*1000);
  return distance;
}
