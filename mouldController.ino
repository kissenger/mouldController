
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

#define RUN_MODE MODE_DEPLOYED
//#define RUN_MODE MODE_TEST_LOCAL
//#define RUN_MODE MODE_TEST_LIVE

//*********************************

//- Set up run modes and server configs --------------------------------------

#if RUN_MODE != MODE_DEPLOYED
  #define DEBUG_PRINT(x)   Serial.print(x);
  #define DEBUG_PRINTLN(x) Serial.println(x);
  #define DEBUG_WRITE(x)   Serial.write(x);
#else
  #define DEBUG_PRINT(x);
  #define DEBUG_PRINTLN(x);
  #define DEBUG_WRITE(x);
#endif

#if RUN_MODE == MODE_TEST_LIVE
  bool isDeployed = false;
  char ssid[] = "IvyTerrace";
  char host[] = "www.thingummy.cc";
  char path[] = "/iot/api/new-data";
  int port = 80;
  float readInterval = 0.1;     // minutes  
  
#elif RUN_MODE == MODE_TEST_LOCAL 
  bool isDeployed = false;
  char ssid[] = "IvyTerrace";
  const IPAddress host(192,168,1,64);
  char path[] = "/api/new-data";  
  int port = 3000;
  float readInterval = 0.1;     // minutes
  
#elif RUN_MODE == DEPLOYED
  bool isDeployed = true;
  char ssid[] = "IvyTerrace_EXT";
  char host[] = "www.thingummy.cc";
  char path[] = "/iot/api/new-data";  
  int port = 80;
  int readInterval = 15;     // minutes
#endif

//- Initialise Variables -----------------------------------------------------

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

//- Setup --------------------------------------------------------------------

void setup() {

  #if RUN_MODE != MODE_DEPLOYED 
    Serial.begin(9600);
    while (!Serial);
    DEBUG_PRINTLN();
  #endif
  
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

//- loop --------------------------------------------------------------

void loop() {

  // blocking check on wifi connection
  // LED is solid on until wifi connection is achieved
  if (WiFi.status() != WL_CONNECTED) {
    DEBUG_PRINT("Connecting to " + String(ssid));
    digitalWrite(LED_BUILTIN, HIGH);
    while (WiFi.status() != WL_CONNECTED) {
      WiFi.begin(ssid, pass);
      DEBUG_PRINT(".");
      delay(2000);
    }
    digitalWrite(LED_BUILTIN, LOW);
    DEBUG_PRINTLN("...OK");
  }

  // print any server response to serial port - very useful for debugging
  #if RUN_MODE != DEPLOYED
    while (client.available()) {
      char c = client.read();
      DEBUG_WRITE(c);
    }
  #endif

  unsigned long now = millis();
  
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
    
    client.stop();

    // blocking check for server connection - should unblock when server becomes available
    // note status takes time to wait for server response before timing out, which delys indication of an error
    while (!client.connect(host, port)) {
      digitalWrite(LED_BUILTIN, HIGH);
      DEBUG_PRINTLN("Server connection lost, retrying");
    }
    digitalWrite(LED_BUILTIN, LOW);

    // post data 
    serializeJson(root, json);
    DEBUG_PRINTLN(json);
    String requestBody = json;
    postData(requestBody);

  }
}


//- functions ---------------------------------------------------------

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
  client.println("POST " + String(path) + " HTTP/1.1");
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
