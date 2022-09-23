
/*
 * With multiple sensor types it's less easy to avoid duplication of code, wo this approach is quick and dirty but it works
 */

#include <WiFiNINA.h>
#include <Adafruit_AHTX0.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <BMP180I2C.h>

#define BMPADDR 0x77
#define TCAADDR 0x70
WiFiClient client;

// deploy config
char ssid[] = "IvyTerrace_EXT";
char server[] = "www.thingummy.cc";
int port = 80;
int readInterval = 15;     // minutes
bool isDeployed = true;

// test config
//char ssid[] = "IvyTerrace";
//const IPAddress server(192,168,1,64);
//int port = 3000;
//int readInterval = 0.1;     // minutes
//bool isDeployed = false;

char pass[] = "qwertyuiopisthetoprowofkeysonakeyboard";
unsigned long lastBlinkTime = 0;    //will store last time Wi-Fi information was updated
unsigned long lastSensorTime = 0;
int nSamples = 5;    //average over this number of samples
int sampleDelay = 100;   //ms delay between samples
bool isFirstLoop = true;
const unsigned long readSensorInterval = readInterval * 60UL * 1000UL;
StaticJsonDocument<512> doc;
char json[512];
BMP180I2C bmpOutside(0x77);
Adafruit_AHTX0 ahtInside;
Adafruit_AHTX0 ahtOutside;
sensors_event_t ahtHumidity, ahtTemp;

void setup() {
  
  Serial.begin(9600);
  delay(500);
  if (!isDeployed) {
    Serial.println("Setup...");
  }
  
  Wire.begin();
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  // Sensor 1
  selectMuxChannel(0);
  ahtInside.begin();

  // Sensors 2 & 3
  selectMuxChannel(1);
  ahtOutside.begin();
  bmpOutside.begin();

}

void loop() {

  unsigned long now = millis();

  // blink LED with interval equal to signal strength
  if (WiFi.status() == WL_CONNECTED) {
    if (now - lastBlinkTime >= WiFi.RSSI() * -10) {
      lastBlinkTime = now;
      digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    }
  } else {
    digitalWrite(LED_BUILTIN, LOW);
    if (!isDeployed) {
      Serial.println("Wifi not connected, searching...");
    }
    connectToWifi();
  }

  if ( (now - lastSensorTime) >= readSensorInterval || isFirstLoop ) {

    lastSensorTime = millis();
    isFirstLoop = false;
    JsonObject root = doc.to<JsonObject>();
    JsonArray arr = root.createNestedArray("data");

    // Sensor 1
    JsonObject sensor1_data = arr.createNestedObject();
    selectMuxChannel(0);
    sensor1_data["sensor_name"] = "ahtInside";
    sensor1_data["sensor_type"] = "ahtx0";
    sensor1_data["time"] = WiFi.getTime();   
    sensor1_data["sensor_found"] = false; 
    sensor1_data["deployed"] = isDeployed;     
    if (ahtInside.getStatus() != 255) {
      float tempSum = 0;
      float rhSum = 0;
      for (int i = 0; i < nSamples; i++) {
        ahtInside.getEvent(&ahtHumidity, &ahtTemp);
        tempSum += ahtTemp.temperature;
        rhSum += ahtHumidity.temperature;
        delay(sampleDelay);      
      };     
      sensor1_data["sensor_found"] = true;
      sensor1_data["temp"] = tempSum / nSamples;
      sensor1_data["rh"] = rhSum / nSamples;   
    };

    // Sensor 2
    JsonObject sensor2_data = arr.createNestedObject();
    selectMuxChannel(1);
    sensor2_data["sensor_name"] = "ahtOutside";
    sensor2_data["sensor_type"] = "ahtx0";
    sensor2_data["time"] = WiFi.getTime();   
    sensor2_data["sensor_found"] = false; 
    sensor2_data["deployed"] = isDeployed;     
    if (ahtOutside.getStatus() != 255) {
      float tempSum = 0;
      float rhSum = 0;
      for (int i = 0; i < nSamples; i++) {
        ahtOutside.getEvent(&ahtHumidity, &ahtTemp);
        tempSum += ahtTemp.temperature;
        rhSum += ahtHumidity.temperature;
        delay(sampleDelay);      
      };     
      sensor2_data["sensor_found"] = true;
      sensor2_data["temp"] = tempSum / nSamples;
      sensor2_data["rh"] = rhSum / nSamples;  
    };
    
    // Sensor 3
    JsonObject sensor3_data = arr.createNestedObject();
    selectMuxChannel(1);
    ahtOutside.getEvent(&ahtHumidity, &ahtTemp);
    sensor3_data["sensor_name"] = "bmpOutside";
    sensor3_data["sensor_type"] = "bmp180";
    sensor3_data["time"] = WiFi.getTime();  
    sensor3_data["sensor_found"] = false;
    sensor3_data["deployed"] = isDeployed;
     
    if (bmpStatus()) {
      
      float pressSum = 0;
      float tempSum = 0;
      
      for (int i = 0; i < nSamples; i++) {
        bmpOutside.measureTemperature(); 
        while (!bmpOutside.hasValue() ) {
          delay(100);
        }; 
        tempSum += bmpOutside.getTemperature();

        bmpOutside.measurePressure(); 
        while (!bmpOutside.hasValue()) delay(100);
        pressSum += bmpOutside.getPressure();

        sensor3_data["sensor_found"] = true;
        sensor3_data["temp"] = tempSum / nSamples;
        sensor3_data["press"] = pressSum / nSamples; 
      } 

    } 
        
    serializeJson(root, json);
    if (!isDeployed) {
      Serial.println(json);
    }
    
    // send to server
    if (client.connect(server, port)) {
      String requestBody = json;
      postData(requestBody);
      if (!isDeployed) {
        Serial.println("request sent");
      }
    } else {
      if (!isDeployed) {
        Serial.println("request not sent");
      }
    }

    delay(500);

      
  }
}

bool bmpStatus(){
  if(bmpOutside.measureTemperature()) {
    delay(1000);
    return true;
  } 
  return false;
}

void postData(String body) {
  // send HTTP request header
  // https://stackoverflow.com/questions/58136179/post-request-with-wifinina-library-on-arduino-uno-wifi-rev-2
  client.println("POST /iot/api/new-data HTTP/1.1");
  client.println("Host: " + String(server));
  client.println("Content-Type: application/json");
  client.println("Accept: */*");
  client.println("Cache-Control: no-cache");
  client.println("Accept-Encoding: gzip, deflate");
  client.println("Accept-Language: en-us");
  client.println("Content-Length: " + String(body.length()));
  client.println("Connection: close");
  client.println(); // end HTTP request header
  client.println(body);
}

void connectToWifi() {
  while (WiFi.status() != WL_CONNECTED) {
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    delay(100);
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));    
//    Serial.print("Attempting connection to ");
//    Serial.print(ssid);
//    Serial.println("...");
    WiFi.begin(ssid, pass);
    delay(10000);
  }
//  Serial.println("Connected!");
}

void selectMuxChannel(int i) {
  if (i > 7) return;
  Wire.beginTransmission(TCAADDR);
  Wire.write(1 << i);
  Wire.endTransmission();
}
