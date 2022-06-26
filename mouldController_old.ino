
#include <SoftwareSerial.h> 
#include <Adafruit_AHTX0.h>

#define PIN_RELAY       6
#define PIN_LCD_RX       2
#define PIN_LCD_TX       3
#define PIN_OPL_RX       10
#define PIN_OPL_TX       11
#define PIN_OPL_RESET    12
#define PIN_SWITCH       8
#define OPEN            false         // for tracking relayState
#define CLOSED          true

#define RHI_TOL 2                     // units is %RH; relay will close at rhi = 1+RHI_TOL and open at rhi = 1-RHI_TOL
#define TEMP_TOL 2                    // units is degs; relay will close temp = MIN_ROOM_TEMP and open at temp = MIN_ROOM_TEMP + TEMP_TOL
#define MIN_ROOM_TEMP 5              // temp
#define READ_SENSOR_INTERVAL 0.25     // minutes
#define SAVE_DATA_INTERVAL 15

#define MODE_MONITOR 0
#define MODE_CONTROL 1

bool relayState = CLOSED;
bool isHighHum = false;
bool isLowTemp = false;
bool isFirstLoop = true;

byte dayIndex = 0;                    // number from 0 to 6
unsigned long dayTimer = 0;
unsigned long weekArray[7];
unsigned long riskTimeDay = 0;        // millis
unsigned long riskTimeWeek = 0;       // millis
unsigned long lastSensorTime = 0;
unsigned long lastLogTime = 0;
unsigned long now;
const unsigned long readSensorInterval = READ_SENSOR_INTERVAL * 60UL * 1000UL;
const unsigned long saveDataInterval = SAVE_DATA_INTERVAL * 60UL * 1000UL;

char outputString[70];

int rh;     // relative humidity is stored as 10ths of % eg 65.2%RH is stored as 652
int temp;   // temperature is stored as 10ths of a degree eg 13.34degC is stored as 133
int rhc;  
byte rhi;    // RH index is stored as 100ths eg 0.45 is stored as 45

SoftwareSerial lcd(PIN_LCD_RX, PIN_LCD_TX);       // setup software serial (RX,TX) for lcd
SoftwareSerial openLog(PIN_OPL_RX, PIN_OPL_TX);   // setup software serial (RX,TX) for openlog
Adafruit_AHTX0 ahtSensor;                         // setup humidity sensor

void setup() {
  
  pinMode(PIN_RELAY, OUTPUT);       
  pinMode(PIN_OPL_RESET, OUTPUT);
  pinMode(PIN_SWITCH, INPUT); 
   
  Serial.begin(9600);
  
  lcd.begin(9600);
  lcdInit();
  
  openLog.begin(9600);
  while (!initOpenLog()) {
    Serial.println(F("openLog NOK"));
    lcdPrint("openLog NOK");
  } 
  openLog.println(F("now, temp, rh, rhCrit, rhi, relayState, isLowTemp, isHighHum, riskTimeDay, riskTimeWeek"));
 
  ahtSensor.begin();

}

void loop() {

  now = millis();

  if ( (now - lastSensorTime) >= readSensorInterval || isFirstLoop ) {

    // get new data
    sensors_event_t ahtHumidity, ahtTemp;
    ahtSensor.getEvent(&ahtHumidity, &ahtTemp);
    
    rh = ahtHumidity.relative_humidity * 10;
    temp = ahtTemp.temperature * 10;
    rhc = rhCrit();
    rhi = (float) rh / rhc * 100;

    // logic to control relay
    if (!isHighHum) {
      if (temp < MIN_ROOM_TEMP * 10.0) { 
        relayState = CLOSED;
        isLowTemp = true;
      } else if (temp > MIN_ROOM_TEMP * 10.0 + TEMP_TOL) {     
        relayState = OPEN;
        isLowTemp = false;
      }
    }

    if (!isLowTemp) {     
      if (rhi > 100 + RHI_TOL) {
        relayState = CLOSED;
        isHighHum = true; 
      } else if (rhi < 100 - RHI_TOL) {  
        relayState = OPEN;
        isHighHum = false;
      }
    }

    // track amount of time spent in risk zone
    if (rhi > 100 - RHI_TOL) {
      riskTimeDay += readSensorInterval;
      riskTimeWeek += readSensorInterval;
    }    
    
    if (dayTimer > 24UL * 60UL * 60UL * 1000UL) { 
      dayTimer = 0;
      weekArray[dayIndex] = riskTimeDay;
      riskTimeDay = 0;      
      riskTimeWeek = 0;
      for (byte i = 0; i < 7; i++) {
        riskTimeWeek += weekArray[i];  
      }
      dayIndex = ++dayIndex % 7;
    }
    dayTimer += readSensorInterval;
    
    // set pins
    digitalWrite(PIN_RELAY, relayState);

    // logging
    lcdUpdate();
     
    getOutputString();
    
    if (relayState == CLOSED) {
      openLog.println(outputString);
      Serial.print("*");
      lastLogTime = now;      
    } else { // relayState == OPEN
      if ( (now - lastLogTime) >= saveDataInterval || isFirstLoop) {
        openLog.println(outputString);
        Serial.print("*");        
        lastLogTime = now;
      }
    }
    
    Serial.println(outputString);
    
    // tidy up
    lastSensorTime = now;
    isFirstLoop = false;

  }

}



// Returns critical RH for mould, calculated from the current value of the global variable, temp
// Note that as for RH, rhCrit is stored as RH*10
// Risk curve based on data from http://dpcalc.org/
int rhCrit() {
  
  if (temp <= 20) {         // 2degs
    return 1000;            // 100%
  } else if (temp >= 240) { // 24deg
    return 650;             // 65%
  } else {
    return 0.00168 * temp * temp + -1.5741 * temp + 931.37;
  }
  
}


// Initialise LCD
// Datasheet: https://cdn.sparkfun.com/assets/9/d/4/e/f/SerLCD_v2_5ApplicationNote_r1_2.pdf
void lcdInit() {

  // turn on display
  lcd.write(0xFE);  
  lcd.write(0x0C);   
  delay(10); 
  
  // clear display
  lcd.write(0xFE);  
  lcd.write(0x01);   
  delay(10);    

  // turn off backlight
  lcd.write(0x7C);  
  lcd.write(0x80);   
  delay(10); 

}


void lcdUpdate() {

  char lcdStr[32];
  char timeStr[12];
  char tempBuf[8];
  char rhiBuf[8];
  char rhBuf[8];
  
  strcpy(timeStr, readableTime(riskTimeWeek));
  timeStr[8] = '\0';       //drop seconds from readable time 
    
  dtostrf(temp/10.0, 5, 1, tempBuf);
  dtostrf(rh/10.0, 5, 1, rhBuf);  
  dtostrf(rhi/100.0, 4, 2, rhiBuf);

  sprintf(lcdStr, "%s\337C %sRHI %sRH %s", tempBuf, rhiBuf, rhBuf, timeStr);
   
  lcdPrint(lcdStr);
  
}

void lcdPrint(char * lineStr) {

  lcd.write(0xFE);  
  lcd.write(128);
  delay(10);    
  lcd.write(lineStr);

}



// Return a formatted string to print to serial and SD card
char getOutputString() {
  
  char nowStr[10];
  char dayTimeStr[10];
  char weekTimeStr[10];
  
  strcpy(nowStr, readableTime(now));
  strcpy(dayTimeStr, readableTime(riskTimeDay));
  strcpy(weekTimeStr, readableTime(riskTimeWeek));
    
  sprintf(outputString, "%s, %d, %d, %d, %d, %d, %d, %d, %s, %s", 
    nowStr, temp, rh, rhc, rhi, relayState, isLowTemp, isHighHum, dayTimeStr, weekTimeStr);

}


// Create a string in the form "dd:hh:mm:ss" from number of milliseconds
char * readableTime(unsigned long ms) {

  char static buff[12];

  int days = ms / 86400000;                                
  int hrs = (ms % 86400000) / 3600000;              
  int mins = ((ms % 86400000) % 3600000) / 60000;
  int secs = (((ms % 86400000) % 3600000) % 60000) / 1000;
  
  sprintf(buff, "%02d:%02d:%02d:%02d", days, hrs, mins, secs);

  return buff;
}


// Reset openLog by applying LOW to GRN pin; ensures openLog is initialised and forces new file
// openLog will then send "12<" indicating it is ready
// function returns true if characters are recieved; false otherwise
bool initOpenLog() {

  unsigned long initTime = millis();
       
  //Reset OpenLog so we can listen for '<' character
  digitalWrite(PIN_OPL_RESET, LOW);
  delay(60);
  digitalWrite(PIN_OPL_RESET, HIGH);

  // check for '<', but dont wat for more than 3s
  while (millis() - initTime < 3000){ 

    if(openLog.available()) {
      
      char c = openLog.read();
      Serial.print(c);
            
      if(c == '<') {
        Serial.println();
        return true;
      }
      
    }
  }

  return false;
  
}
