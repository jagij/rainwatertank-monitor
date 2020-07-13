#include <ESP8266WiFi.h>
#include "Adafruit_MQTT.h"
#include "Adafruit_MQTT_Client.h"

#include <ArduinoSort.h>

#include "credentials.h"

/*
 * Partially from:
* Ultrasonic Sensor HC-SR04 and Arduino Tutorial
*
* by Dejan Nedelkovski,
* www.HowToMechatronics.com
*
*/

// define these in credentials.h, or edit them here if you don't want to share the code publicly.
//#define WLAN_SSID       "...your SSID..."
//#define WLAN_PASS       "...your password..."
//
//#define AIO_SERVER      "io.adafruit.com"
//#define AIO_SERVERPORT  8883                   // 8883 for MQTTS
//#define AIO_USERNAME    "...your AIO username (see https://accounts.adafruit.com)..."
//#define AIO_KEY         "...your AIO key..."

// custom well stuff
#define AIO_FEED_NAME "regenput"
const int WELL_EMPTY = 1943; // level at which the well is empty
const int WELL_VOLUME = 5000; // total volume of the well
const int WELL_DEPTH = 1800; // high water level

// defines pins numbers
const int trigPin = 4;
const int echoPin = 5;

// delay between two measurements, increase if too much echos
const int measureDelayMs = 60; // minimal 60ms per docs HC-SR04
const int measurementsForModus = 100;
const int groupNumMeasurements = 100;
const int cutoffDistanceMm = 30;
const int filterMinDistance = 350; // filter out nasty reflections around 300mm
const int maxMeasureRetries = 10; // prevent endless loop when sensor data is bad
const int defaultDistance = 2000; // > WELL_EMPTY

// defines variables
int estimateMean = -1;

// WiFiFlientSecure for SSL/TLS support
WiFiClientSecure client;

// Setup the MQTT client class by passing in the WiFi client and MQTT server and login details.
Adafruit_MQTT_Client mqtt(&client, AIO_SERVER, AIO_SERVERPORT, AIO_USERNAME, AIO_KEY);

// io.adafruit.com SHA1 fingerprint
//const char* fingerprint = "AD 4B 64 B3 67 40 B5 FC 0E 51 9B BD 25 E9 7F 88 B6 2A A3 5B";
//const char* fingerprint = "26 96 1C 2A 51 07 FD 15 80 96 93 AE F7 32 CE B9 0D 01 55 C4";
//const char* fingerprint = AIO_SSL_FINGERPRINT;
// using: echo | openssl s_client -connect host.example.com:443 | openssl x509 -fingerprint -noout
const char* fingerprint = "59:3C:48:0A:B1:8B:39:4E:0D:58:50:47:9A:13:55:60:CC:A0:1D:AF";

// Setup a feed called 'test' for publishing.
// Notice MQTT paths for AIO follow the form: <username>/feeds/<feedname>
Adafruit_MQTT_Publish test = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/" AIO_FEED_NAME);

void setup() {
  pinMode(trigPin, OUTPUT); // Sets the trigPin as an Output
  pinMode(echoPin, INPUT); // Sets the echoPin as an Input
  Serial.begin(115200); // Starts the serial communication
  delay(10);

  Serial.println(F("Post rain water well sensor data to Adafruit IO"));

  Serial.println(); Serial.println();
  Serial.print("Connecting to ");
  Serial.println(WLAN_SSID);

  delay(1000);

  WiFi.begin(WLAN_SSID, WLAN_PASS);
  delay(2000);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  Serial.println("WiFi connected");
  Serial.println("IP address: "); Serial.println(WiFi.localIP());

  // check the fingerprint of io.adafruit.com's SSL cert
  verifyFingerprint();

  // start connecting, first time fails otherwise
  MQTT_connect();
}

void loop() {
  // Ensure the connection to the MQTT server is alive (this will make the first
  // connection and automatically reconnect when disconnected).  See the MQTT_connect
  // function definition further below.
  MQTT_connect();

  if (estimateMean == -1) {
    estimateMean = getModus();
    Serial.print(F("Modus:"));
    Serial.println(estimateMean);
  }
  if (updateAverageDistance() != 0) {
    return;
  }
  // estimateMean is now filled in
  Serial.print(F("Avg distance: "));
  Serial.println(estimateMean);
  
  // Now we can publish stuff!
  const int contents = calculateWaterVolume();
  Serial.print(F("\nSending val "));
  Serial.print(contents);
  Serial.print(F(" to test feed..."));
  if (! test.publish(contents)) {
    Serial.println(F("Failed"));
  } else {
    Serial.println(F("OK!"));
  }
  Serial.println(); Serial.println();
}

int calculateWaterVolume() {
  // estimate contents using WELL_DEPTH and WELL_VOLUME
  return (WELL_EMPTY - estimateMean) * WELL_VOLUME / WELL_DEPTH;
}

int getModus() {
  Serial.println(F("Getting modus"));
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
    if (currentValue == defaultDistance) {
      // ignore default distance
      continue;
    }
    currentCount++;
  }
  if (currentCount > maxCount) {
    maxCount = currentCount;
    maxValue = currentValue;
  }
  return maxValue;
}

int updateAverageDistance() {
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
    return -1;
  }
  estimateMean = sum/numMeasurements;
  return 0;
}

int getDistanceMm() {
  for (int i = 0; i != maxMeasureRetries; i++) {
    const int distance = getDistanceMmRaw();
    if (distance < filterMinDistance) {
      // Prints the distance on the Serial Monitor
      Serial.print(F("Ignored distance: "));
      Serial.println(distance);
      continue;
    }
    // Prints the distance on the Serial Monitor
    Serial.print(F("Measured distance: "));
    Serial.println(distance);
    return distance;
  }
  // Prints the distance on the Serial Monitor
  Serial.print(F("Default distance: "));
  Serial.println(defaultDistance);
  return defaultDistance;
}

int getDistanceMmRaw() {
  // Clears the trigPin
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  // Sets the trigPin on HIGH state for 10 micro seconds
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  // Reads the echoPin, returns the sound wave travel time in microseconds
  const long duration = pulseIn(echoPin, HIGH);
  // Calculating the distance
  const int distance = duration*0.034/2*10;

  // delay for next measurement
  delayMicroseconds(measureDelayMs*1000);
  return distance;
}

void verifyFingerprint() {

  const char* host = AIO_SERVER;

  Serial.print("Connecting to ");
  Serial.println(host);

  client.setFingerprint(fingerprint);

  if (! client.connect(host, AIO_SERVERPORT)) {
    Serial.println("Connection failed. Halting execution.");
    while(1);
  }

}

// Function to connect and reconnect as necessary to the MQTT server.
// Should be called in the loop function and it will take care if connecting.
void MQTT_connect() {
  int8_t ret;

  // Stop if already connected.
  if (mqtt.connected()) {
    return;
  }

  Serial.print("Connecting to MQTT... ");

  uint8_t retries = 3;
  while ((ret = mqtt.connect()) != 0) { // connect will return 0 for connected
       Serial.println(mqtt.connectErrorString(ret));
       Serial.println("Retrying MQTT connection in 5 seconds...");
       mqtt.disconnect();
       delay(5000);  // wait 5 seconds
       retries--;
       if (retries == 0) {
         // basically die and wait for WDT to reset me
         while (1);
       }
  }

  Serial.println("MQTT Connected!");
}
