
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

#define MODE_DEPLOYED 0
#define MODE_TEST_LOCAL 1
#define MODE_TEST_LIVE 2

//*********************************
#define RUN_MODE MODE_TEST_DEPLOYED
//*********************************

#if RUN_MODE != MODE_DEPLOYED
  #define DEBUG_PRINT(x)   Serial.print(x);
  #define DEBUG_PRINTLN(x) Serial.println(x);
#else
  #define DEBUG_PRINT(x);
  #define DEBUG_PRINTLN(x);
#endif

#if RUN_MODE == MODE_TEST_LIVE
  bool isDeployed = false;
  int readInterval = 0.1;     // minutes
  char ssid[] = "IvyTerrace";
  char host[] = "www.thingummy.cc";
  char path[] = "/iot/api/new-data";
  int port = 80;
  
#elif RUN_MODE == MODE_TEST_LOCAL 
  bool isDeployed = false;
  int readInterval = 0.1;     // minutes
  char ssid[] = "IvyTerrace";
  const IPAddress host(192,168,1,64);
  char path[] = "/api/new-data";  
  int port = 3000;
  
#elif RUN_MODE == DEPLOYED
  bool isDeployed = true;
  char ssid[] = "IvyTerrace_EXT";
  char host[] = "www.thingummy.cc";
  char path[] = "/iot/api/new-data";  
  int port = 80;
  int readInterval = 15;     // minutes
#endif

WiFiClient client;
char pass[] = "qwertyuiopisthetoprowofkeysonakeyboard";
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
  delay(1000);
  DEBUG_PRINTLN();
  
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

  // blocking check on wifi connection
  // LED is solid on until wifi connection is achieved
  if (WiFi.status() != WL_CONNECTED) {
    DEBUG_PRINT("Attempting to connect to " + String(ssid));
    digitalWrite(LED_BUILTIN, HIGH);
    while (WiFi.status() != WL_CONNECTED) {
      WiFi.begin(ssid, pass);
      DEBUG_PRINT(".");
      delay(2000);
    }
    DEBUG_PRINTLN("...OK");
  }


  // blocking check on server connection 
  // LED will flash on and off at 1-sec intervals 
  if (!client.connected()) {
    DEBUG_PRINT("Attempting to connect to " + String(host) + String(path));
    while (!client.connect(host, port)) {
      digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
      DEBUG_PRINT(".");
      delay(500);
    }
    DEBUG_PRINTLN("...OK");    
  }  

  // main loop
  unsigned long now = millis();
  digitalWrite(LED_BUILTIN, LOW);

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
        while (!bmpOutside.hasValue()) delay(100);
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
    DEBUG_PRINTLN(json);
    //String requestBody = json;
    postData(json);

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
  client.println("POST " + String(path) + " TTP/1.1");
  client.println("Host: " + String(host));
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



void selectMuxChannel(int i) {
  if (i > 7) return;
  Wire.beginTransmission(TCAADDR);
  Wire.write(1 << i);
  Wire.endTransmission();
}
