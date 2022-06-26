#include <WiFiNINA.h>
#include <Adafruit_AHTX0.h>
#include <ArduinoJson.h>
#include <Wire.h>

#define TCAADDR 0x70
WiFiClient client;

// deploy config
char ssid[] = "IvyTerrace_EXT";
char server[] = "www.thingummy.cc";
int port = 80;
int readInterval = 10;     // minutes
bool isDeployed = true;

// test config
/*
char ssid[] = "IvyTerrace";
const IPAddress server(192,168,1,64);
int port = 3000;
int readInterval = 0.1;     // minutes
bool isDeployed = false;
*/

char pass[] = "qwertyuiopisthetoprowofkeysonakeyboard";
unsigned long lastBlinkTime = 0;    //will store last time Wi-Fi information was updated
unsigned long lastSensorTime = 0;
bool isFirstLoop = true;
const unsigned long readSensorInterval = readInterval * 60UL * 1000UL;
StaticJsonDocument<128> doc;
char json[128];

struct sensor {
  char* sensorName;
  char* sensorType;
  bool isFound;
  Adafruit_AHTX0 sensorObject;
};

sensor sensors[2];

void setup() {
  
  Serial.begin(9600);
  delay(2000);
  Serial.println("Setup...");
  
  Wire.begin();
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
  
  sensors[0].sensorName = "ahtInside";
  sensors[0].sensorType = "ahtx0"; 
  sensors[0].isFound = false;
  
  sensors[1].sensorName = "ahtOutside";
  sensors[1].sensorType = "ahtx0"; 
  sensors[1].isFound = false;

  //non-blocking check for aht
  for (int i = 0; i < 2; i++) {
    selectSensor(i);
    if (sensors[i].sensorObject.begin()) {
      sensors[i].isFound = true;
    }
  }

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
    Serial.println("Wifi not connected, searching...");
    connectToWifi();
  }

  if ( (now - lastSensorTime) >= readSensorInterval || isFirstLoop ) {

    lastSensorTime = millis();
    isFirstLoop = false;

    for (int i = 0; i < 2; i++) {

        
        selectSensor(i);
        sensors_event_t ahtHumidity, ahtTemp;
        sensors[i].sensorObject.getEvent(&ahtHumidity, &ahtTemp);
        
        doc["sensor_name"] = sensors[i].sensorName;
        doc["sensor_type"] = sensors[i].sensorType;
        doc["sensor_found"] = sensors[i].isFound;      
        doc["time"] = WiFi.getTime();
        doc["temp"] = sensors[i].isFound ? ahtTemp.temperature : 0.0;
        doc["rh"] = sensors[i].isFound ? ahtHumidity.relative_humidity : 0.0;
        serializeJson(doc, json);
  
        Serial.println(json);
        
        // send to server
        if (client.connect(server, port)) {
          String requestBody = json;
          postData(requestBody);
          Serial.println("request sent");
        } else {
          Serial.println("request not sent");
        }

        delay(500);

    }
      
  }
}


void postData(String body) {
  // send HTTP request header
  // https://stackoverflow.com/questions/58136179/post-request-with-wifinina-library-on-arduino-uno-wifi-rev-2
  client.println("POST /api/new-data HTTP/1.1");
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
  
    // attempt to connect to Wi-Fi network:
  while (WiFi.status() != WL_CONNECTED) {
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    delay(100);
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));    
    Serial.print("Attempting connection to ");
    Serial.println(ssid);
    WiFi.begin(ssid, pass);
    delay(10000);
  }
  Serial.println("Connected");
}

void selectSensor(int i) {
  if (i > 7) return;
  Wire.beginTransmission(TCAADDR);
  Wire.write(1 << i);
  Wire.endTransmission();
}
