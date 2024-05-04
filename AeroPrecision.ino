/**
==================================================================
The Doppler Project
==================================================================
Sensors:
- DFRobot PH Sensor
- DFRobot TDS Sensor
- Temperature Sensor
- Water Level Sensor
- DS1302 Time Module
- SD Card Module
- Sim800 Module

**/

#include <virtuabotixRTC.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <EEPROM.h>
#include "GravityTDS.h"
#include <SPI.h>
#include <SD.h>

// DECLARED PINS ////////////////////////////////////////////////
#define PIN_PHSENSOR A0
#define PIN_WATERLEVEL A1
#define PIN_TDSSENSOR A2
#define PIN_TEMPERATURE 57 //or A3
#define PIN_SDC 53
#define SMS Serial1
//TIME MODULE //////////////////////
// CLK -> 69 / A15 , DAT -> 68 / A14, Reset -> 67 / A13 //Analog pins converted to Digital Pins
virtuabotixRTC myRTC(69, 68, 67); // If you change the wiring change the pins here also

// Global variables //////////////////////////////////////////////////////////////
String DATA_DATETIME = ""; 
String DATA_FILENAME = ""; 
String DATA_TEXTLINE = "";
String DATA_TEXTMSG  = "";

// ERROR FLAGS ////////////////////////////////////////////////////////////////////
//This will determine the LOW / ALERT Values
//Once Data is Below this, it will send a message

String DEFAULT_PHONE = "+639668722051"; //the default number where the system will send
float ERR_PHLEVEL = 7;
float ERR_TDS = 1000;
int ERR_TEMP = 1000;
float ERR_WATERLEVEL = 350;

// SENSOR DATA ////////////////////////////////////////////////////////////////////
// WATERLEVEL /////////////////////////
int DATA_WATERLEVEL = 0;
String STR_WATERLEVEL = "";
// PH LEVEL ////////////////////////
float DATA_PHLEVEL;
// TDS /////////////////////////////
GravityTDS gravityTds;
float DATA_TDS;
// TEMPERATURE /////////////////////
int DATA_TEMP = 0;
///////////////////////////////////////////////////////////////////////////////////

void setup() {
  Serial.begin(9600);

  gravityTds.setPin(PIN_TDSSENSOR);
  gravityTds.setAref(5.0);  
  gravityTds.setAdcRange(1024);  
  gravityTds.begin();  

  // Set the current date, and time in the following format:
  // seconds, minutes, hours, day of the week, day of the month, month, year
  // myRTC.setDS1302Time(18, 28, 20, 6, 13, 4, 2024);
  /*
  Serial.print("Initializing SD card...");

  pinMode(PIN_SDC, OUTPUT);

  if (!SD.begin(PIN_SDC)) {
    Serial.println("initialization failed!");
    while (1);
  }
  Serial.println("initialization done.");
  delay(3000);

  */
  SMS.begin(9600);
  SMS.println("AT"); //Once the handshake test is successful, it will back to OK
  updateSMSSerial();
  SMS.println("AT+CSQ"); //Signal quality test, value range is 0-31 , 31 is the best
  updateSMSSerial();
  SMS.println("AT+CCID"); //Read SIM information to confirm whether the SIM is plugged
  updateSMSSerial();
  SMS.println("AT+CREG?"); //Check whether it has registered in the network
  updateSMSSerial();
  SMS.println("AT+CMGF=1"); // Configuring TEXT mode
  updateSMSSerial();
  SMS.println("AT+CNMI=1,2,0,0,0"); // Decides how newly arrived SMS messages should be handled
  updateSMSSerial();

}

void loop() {
  // Get current timex
  getTime();
  getWaterLevel();
  getPH_Level();
  get_temperature();
  get_TDS();
  
  DataFormatter();
  CheckNewSMS();
  TaskController();


  Serial.println(DATA_TEXTMSG);
  Serial.println(DATA_TEXTLINE);
  Serial.println(DATA_FILENAME);

  Serial.println("----------------------------------------");

  delay(1000);

}

// SMS ////////////////////////////////////////////////////////////////
void updateSMSSerial() {
  delay(500);
  while (Serial.available()) 
  {
    SMS.write(Serial.read());//Forward what Serial received to Software Serial Port
  }
  while(SMS.available()) 
  {
    Serial.write(SMS.read());//Forward what Software Serial received to Serial Port
  }
}

void SendMessage(String phoneNumber, String message) {

  //phoneNumber = phoneNumber.c_str();
  //message = message.c_str();

  SMS.println("AT+CMGS=\""+phoneNumber+"\"");//change ZZ with country code and xxxxxxxxxxx with phone number to sms
  updateSMSSerial();
  SMS.print(message); //text content
  updateSMSSerial();
  SMS.write(26); 
  updateSMSSerial();
}


void CheckNewSMS() {
  //SMS.println("AT+CMGL=\"ALL\""); // List unread messages
  delay(500);
  
  while (SMS.available()) {
    String response = SMS.readString();
    Serial.println("RECEIVED TRIGGER");
    Serial.println(response);
    if (response.indexOf("+CMT:") != -1) {
      // Extract phone number and message content
      Serial.println("READMSG TRIGGER");

      String CellNumtemp = response.substring(response.indexOf("+63"));
      String phoneNumber = CellNumtemp.substring(0,13);
      Serial.println("Phone: " + phoneNumber);  

      String message = response.substring(51, response.length()-2);
      Serial.println("Message: " + message);
      
      SMS_Command(message, phoneNumber);

      // Delete the message from SIM memory
      SMS.println("AT+CMGD=1,0");
      delay(500);
      
 
    }
  }
}

void SMS_Command(String message, String phoneNumber) {
  if(message == "NOW") {
        Serial.println("Here");
        SendMessage(phoneNumber, DATA_TEXTMSG);
  } else {
    //ehh.. do nothing? 
  }
}

// TIME BASED ACTIONS /////////////////////////////////////////////////
void getTime() {
  myRTC.updateTime(); 
  DATA_DATETIME = myRTC.year; 
  DATA_DATETIME.concat("/");
  DATA_DATETIME.concat(myRTC.month);
  DATA_DATETIME.concat("/");
  DATA_DATETIME.concat(myRTC.dayofmonth);
  DATA_DATETIME.concat(" ");
  DATA_DATETIME.concat(myRTC.hours);
  DATA_DATETIME.concat(":");
  DATA_DATETIME.concat(myRTC.minutes);
  DATA_DATETIME.concat(":");
  DATA_DATETIME.concat(myRTC.seconds);
  
  //Set Filename based on Date
  DATA_FILENAME = "DATA_";
  DATA_FILENAME.concat(myRTC.year); 
  DATA_FILENAME.concat("_");
  DATA_FILENAME.concat(myRTC.month);
  DATA_FILENAME.concat("_");
  DATA_FILENAME.concat(myRTC.dayofmonth);
  DATA_FILENAME.concat(".txt");
}

// TASK CONTROLLER /////////////////////////////////////////////////////
// The general purpose of the Task Controller is to handle all data-related
// functionalities
int sd_current_minute = 0; 
int sms_current_minute = 0; 

void TaskController() {
  // Save Data every 5 Minutes based on Time. /////////////////////////
  // e.g 10:00, 10:05, 10:10 etc..
  if (myRTC.minutes % 5 == 0 && myRTC.minutes != sd_current_minute) {
    sd_current_minute = myRTC.minutes; //used to save data only once
    Serial.println("****TRIGGER MINUTES");
    //Save Data to SD Card
    SaveData();
  } 

  if (myRTC.minutes % 30 == 0 && myRTC.minutes != sms_current_minute) {
    sms_current_minute = myRTC.minutes; //used to send only 1 sms per minute//
    Serial.println("****TRIGGER SMS ");

    String message = "";

    if(ERR_PHLEVEL >= DATA_PHLEVEL) {
        message = "ALERT! CHECK PH LEVEL! \n";
        message.concat(DATA_TEXTMSG);
        SendMessage(DEFAULT_PHONE, message);
    }

    if(ERR_TDS >= DATA_TDS) {
        message = "ALERT! CHECK TDS! \n";
        message.concat(DATA_TEXTMSG);
        SendMessage(DEFAULT_PHONE, message);
    }

    if(ERR_TEMP > DATA_TEMP) {
        message = "ALERT! WATER TEMP! \n";
        message.concat(DATA_TEXTMSG);
        SendMessage(DEFAULT_PHONE, message);
    }

    if(ERR_WATERLEVEL >= DATA_WATERLEVEL) {
        message = "ALERT! WATERLEVEL! \n";
        message.concat(DATA_TEXTMSG);
        SendMessage(DEFAULT_PHONE, message);
    }
    
  } 


}

void DataFormatter() {
  // Compile Data to TextLine /////////////////////////////////////////
  // Compiled Data Structure:
  //  DATE_TIME waterlevel;temperature;ph_level;tds_level;
  // Sample:
  // 2024/4/27 10:4:10 365:MID;35C;6.30PH;911.51PPM
  DATA_TEXTLINE = DATA_DATETIME;
  DATA_TEXTLINE.concat(" ");
  DATA_TEXTLINE.concat(DATA_WATERLEVEL);
  DATA_TEXTLINE.concat(":");
  DATA_TEXTLINE.concat(STR_WATERLEVEL);
  DATA_TEXTLINE.concat(";");

  DATA_TEXTLINE.concat(DATA_TEMP);
  DATA_TEXTLINE.concat(" C");
  DATA_TEXTLINE.concat(";");

  DATA_TEXTLINE.concat(DATA_PHLEVEL);
  DATA_TEXTLINE.concat(" PH");
  DATA_TEXTLINE.concat(";");

  DATA_TEXTLINE.concat(DATA_TDS);
  DATA_TEXTLINE.concat(" PPM");

  /////////////////////////////////////////////////////
  DATA_TEXTMSG = DATA_DATETIME;
  DATA_TEXTMSG.concat("\n WaterLevel: ");
  DATA_TEXTMSG.concat(DATA_WATERLEVEL);
  DATA_TEXTMSG.concat(" - ");
  DATA_TEXTMSG.concat(STR_WATERLEVEL);
  
  DATA_TEXTMSG.concat("\n TEMP: ");
  DATA_TEXTMSG.concat(DATA_TEMP);
  DATA_TEXTMSG.concat(" C");

  DATA_TEXTMSG.concat("\n PH Level: ");
  DATA_TEXTMSG.concat(DATA_PHLEVEL);

  DATA_TEXTMSG.concat("\n TDS: ");
  DATA_TEXTMSG.concat(DATA_TDS);
  DATA_TEXTMSG.concat(" PPM");
}

void SaveData() {
   File dataFile = SD.open(DATA_FILENAME, FILE_WRITE);
    // if the file is available, write to it:
    if (dataFile) {
      dataFile.println(DATA_TEXTLINE);
      dataFile.close();
      Serial.println("***SAVED TO SD CARD");
    }
    // if the file isn't open, pop up an error:
    else {
      Serial.println("error opening datalog.txt");
    }
}

// SENSORS ////////////////////////////////////////////////////////////
void getWaterLevel() {
  DATA_WATERLEVEL = analogRead(PIN_WATERLEVEL);
  if (DATA_WATERLEVEL == 0) {
    STR_WATERLEVEL = "EMPTY";
  } 
  else if (DATA_WATERLEVEL > 1 && DATA_WATERLEVEL < 350) {
    STR_WATERLEVEL = "LOW";
  } 
  else if (DATA_WATERLEVEL > 350 && DATA_WATERLEVEL < 400) {
    STR_WATERLEVEL = "MID";
  } 
  else if (DATA_WATERLEVEL > 450){
    STR_WATERLEVEL = "FULL";
  }   
}

void getPH_Level() {
  int buf[10], ph_temp;
  unsigned long int ph_avgValue = 0;
  
  for(int i = 0; i < 10; i++) { 
    buf[i] = analogRead(PIN_PHSENSOR);
    delay(10);
  }
  
  for(int i = 0; i < 9; i++) {        
    for(int j = i + 1; j < 10; j++) {
      if(buf[i] > buf[j]) {
        ph_temp = buf[i];
        buf[i] = buf[j];
        buf[j] = ph_temp;
      }
    }
  }
  
  for(int i = 2; i < 8; i++)                   
    ph_avgValue += buf[i];
  
  DATA_PHLEVEL = (float)ph_avgValue * 5.0 / 1024 / 6; 
  DATA_PHLEVEL = 3.5 * DATA_PHLEVEL;
}

//////////////////////////////////////////////////////////////////////
OneWire oneWire(PIN_TEMPERATURE);
DallasTemperature sensors(&oneWire);

void get_temperature(){
  sensors.requestTemperatures();
  DATA_TEMP = sensors.getTempCByIndex(0);
}

void get_TDS(){
  gravityTds.setTemperature(DATA_TEMP);  
  gravityTds.update();  
  DATA_TDS = gravityTds.getTdsValue();  
}
