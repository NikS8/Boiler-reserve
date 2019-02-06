/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*\
                                                    boilerBack.ino 
                            Copyright © 2018-2019, Zigfred & Nik.S
19.12.2018 v1
03.01.2019 v2 dell <ArduinoJson.h>
10.01.2019 v3 изменен расчет в YF-B5
13.01.2019 v4 createDataString в формате json
15.01.2019 v5 дабавлены данные по температуре коллектора
16.01.2019 v6 обозначены места расположения датчиков температуры
17.01.2019 v7 в именах датчиков температуры последние 2 цифры
19.01.2019 v8 нумерация контуров коллектора слева направо  
03.02.2019 v9 преобразование в формат  F("")
04.02.2019 v10 добавлена функция freeRam()
04.02.2019 v11 добавлены ds18 коллектора
04.02.2019 v12 добавленa "data"
06.02.2019 v13 переменным добавлен префикс "boiler-back-"
06.02.2019 v14 изменение вывода №№ DS18 и префикс заменен на "bb-"
\*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
/*******************************************************************\
Сервер boilerBack выдает данные: 
  аналоговые: 
    датчики трансформаторы тока  
  цифровые: 
    датчик скорости потока воды YF-B5
    датчики температуры DS18B20
/*******************************************************************/

#include <Ethernet2.h>
#include <SPI.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <EmonLib.h>
#include <RBD_Timer.h>

#define DEVICE_ID "boiler-back"
#define VERSION 14

#define RESET_UPTIME_TIME 2592000000  //  =30 * 24 * 60 * 60 * 1000 // reset after 30 days uptime
#define REST_SERVICE_URL "192.168.1.210"
#define REST_SERVICE_PORT 3010
char settingsServiceUri[] = "/settings/boilerBack";
char intervalLogServiceUri[] = "/intervalLog/boilerBack";

// settings
byte mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED};
EthernetServer httpServer(40250);
EthernetClient httpClient;

#define PIN_TRANS_1 A1
#define PIN_TRANS_2 A2
#define PIN_TRANS_3 A3
EnergyMonitor emon1;
EnergyMonitor emon2;
EnergyMonitor emon3;

#define DS18_CONVERSION_TIME 750 // (1 << (12 - ds18Precision))
#define PIN8_ONE_WIRE_BUS 8           //  коллектор
unsigned short ds18DeviceCount8;
OneWire ds18wireBus8(PIN8_ONE_WIRE_BUS);
DallasTemperature ds18Sensors8(&ds18wireBus8);
#define PIN9_ONE_WIRE_BUS 9           //  бойлер
unsigned short ds18DeviceCount9;
OneWire ds18wireBus9(PIN9_ONE_WIRE_BUS);
DallasTemperature ds18Sensors9(&ds18wireBus9);
uint8_t ds18Precision = 11;
/*
byte flowSensorInterrupt = 0; // 0 = digital pin 2
byte flowSensorPin = 2;
unsigned long flowSensorLastTime = 0;
volatile long flowSensorPulseCount = 0;
*/
#define PIN_FLOW_SENSOR 2
#define PIN_INTERRUPT_FLOW_SENSOR 0
#define FLOW_SENSOR_CALIBRATION_FACTOR 5
//unsigned long flowSensorLastTime = 0;
volatile long flowSensorPulseCount = 0;

// time
unsigned long currentTime;
unsigned long flowSensorLastTime;
// settings intervals
unsigned int intervalLogServicePeriod = 10000;
// timers
RBD::Timer intervalLogServiceTimer;
RBD::Timer ds18ConversionTimer;

// TODO optimize variables data length
/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*\
              setup
\*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
void setup()
{
  Serial.begin(9600);
  Ethernet.begin(mac);
  while (!Serial) continue;

  pinMode(PIN_TRANS_1, INPUT);
  pinMode(PIN_TRANS_2, INPUT);
  pinMode(PIN_TRANS_3, INPUT);

  emon1.current(1, 8.4);
  emon2.current(2, 8.4);
  emon3.current(3, 8.4);
/*
  pinMode(flowSensorPin, INPUT);
 // digitalWrite(flowSensorPin, HIGH);
  attachInterrupt(flowSensorInterrupt, flowSensorPulseCounter, RISING);
  sei();
*/
  pinMode(PIN_FLOW_SENSOR, INPUT);
  attachInterrupt(PIN_INTERRUPT_FLOW_SENSOR, flowSensorPulseCounter, FALLING);
  sei();

  httpServer.begin();

  ds18Sensors8.begin();
  ds18DeviceCount8 = ds18Sensors8.getDeviceCount();
  ds18Sensors9.begin();
  ds18DeviceCount9 = ds18Sensors9.getDeviceCount();

  getSettings();
}
  // TODO create function for request temp and save last request time

/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*\
            function getSettings
\*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
void getSettings()
{
  String responseText = doRequest(settingsServiceUri, "");
  // TODO parse settings and fill values to variables
  //intervalLogServicePeriod = 10000;
  //settingsServiceUri 
  //intervalLogServiceUri
  //ds18Precision
  ds18Sensors8.requestTemperatures();
  ds18Sensors9.requestTemperatures();
  intervalLogServiceTimer.setTimeout(intervalLogServicePeriod);
  intervalLogServiceTimer.restart();
  ds18ConversionTimer.setTimeout(DS18_CONVERSION_TIME);
  ds18ConversionTimer.restart();
}

/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*\
            loop
\*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
void loop()
{
  currentTime = millis();
  resetWhen30Days();

    realTimeService();
    intrevalLogService();
}

/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*\
            function realTimeService
\*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
void realTimeService()
{

  EthernetClient reqClient = httpServer.available();
  if (!reqClient) return;

  while (reqClient.available()) reqClient.read();

  String data = createDataString();

  reqClient.println(F("HTTP/1.1 200 OK"));
  reqClient.println(F("Content-Type: application/json"));
  reqClient.print(F("Content-Length: "));
  reqClient.println(data.length());
  reqClient.println();
  reqClient.print(data);

  reqClient.stop();
}

/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*\
            function intrevalLogService
\*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
void intrevalLogService()
{
  if (intervalLogServiceTimer.getInverseValue() <= DS18_CONVERSION_TIME) {
    ds18RequestTemperatures();
  }

  if (intervalLogServiceTimer.onRestart()) {
    String data = createDataString();

    String response = doRequest(intervalLogServiceUri, data);
    Serial.println(response);
  }
}

/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*\
            function ds18RequestTemperatures
\*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
void ds18RequestTemperatures()
{
  if (ds18ConversionTimer.onRestart()) {
    ds18Sensors8.requestTemperatures();
    ds18Sensors9.requestTemperatures();
  }
}

/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*\
            function flowSensorPulseCounter
\*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
void flowSensorPulseCounter()
{
  // Increment the pulse counter
  flowSensorPulseCount++;
}

/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*\
            function createDataString
\*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
String createDataString()
{
  String resultData;

  resultData.concat(F("{"));
  resultData.concat(F("\n\"deviceId\":"));
//  resultData.concat(String(DEVICE_ID));
  resultData.concat(F("\"boiler-back\""));
/*  resultData.concat(F(","));
  resultData.concat(F("\n\"version\":"));
  resultData.concat(VERSION);
*/
  resultData.concat(F(","));
  resultData.concat(F("\n\"data\": {"));

  resultData.concat(F("\n\t\"bb-trans-1\":"));
  resultData.concat(String((float)emon1.calcIrms(1480), 1));
  resultData.concat(F(","));
  resultData.concat(F("\n\t\"bb-trans-2\":"));
  resultData.concat(String((float)emon2.calcIrms(1480), 1));
  resultData.concat(F(","));
  resultData.concat(F("\n\t\"bb-trans-3\":"));
  resultData.concat(String((float)emon3.calcIrms(1480), 1));

  resultData.concat(F(","));
  for (uint8_t index9 = 0; index9 < ds18DeviceCount9; index9++)
  {
    DeviceAddress deviceAddress9;
    ds18Sensors9.getAddress(deviceAddress9, index9);
    resultData.concat(F("\n\t\""));
    for (uint8_t i = 0; i < 8; i++)
    {
      if (deviceAddress9[i] < 16) resultData.concat("0");

      resultData.concat(String(deviceAddress9[i], HEX));
    }
    resultData.concat(F("\":"));
    resultData.concat(ds18Sensors9.getTempC(deviceAddress9));
    resultData.concat(F(","));
  }

  for (uint8_t index8 = 0; index8 < ds18DeviceCount8; index8++)
  {
    DeviceAddress deviceAddress8;
    ds18Sensors8.getAddress(deviceAddress8, index8);
        resultData.concat(F("\n\t\""));
    for (uint8_t i = 0; i < 8; i++)
    {
      if (deviceAddress8[i] < 16) resultData.concat("0");

      resultData.concat(String(deviceAddress8[i], HEX));
    }
    resultData.concat(F("\":"));
    resultData.concat(ds18Sensors8.getTempC(deviceAddress8));
    resultData.concat(F(","));
  }
   
  resultData.concat(F("\n\t\"bb-flow\":"));
  resultData.concat(getFlowData());
  resultData.concat(F("\n\t }"));
  resultData.concat(F(","));
  resultData.concat(F("\n\"freeRam\":"));
  resultData.concat(freeRam());
  resultData.concat(F("\n }"));
 // resultData.concat(F("}"));

  return resultData;
}

/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*\
            function to measurement flow water
\*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
int getFlowData()
{
  //  static int flowSensorPulsesPerSecond;
  unsigned long flowSensorPulsesPerSecond;

  unsigned long deltaTime = millis() - flowSensorLastTime;
  //  if ((millis() - flowSensorLastTime) < 1000) {
  if (deltaTime < 1000)
  {
    return;
  }

  //detachInterrupt(flowSensorInterrupt);
  detachInterrupt(PIN_INTERRUPT_FLOW_SENSOR);
  //     flowSensorPulsesPerSecond = (1000 * flowSensorPulseCount / (millis() - flowSensorLastTime));
  //    flowSensorPulsesPerSecond = (flowSensorPulseCount * 1000 / deltaTime);
  flowSensorPulsesPerSecond = flowSensorPulseCount;
  flowSensorPulsesPerSecond *= 1000;
  flowSensorPulsesPerSecond /= deltaTime; //  количество за секунду

  flowSensorLastTime = millis();
  flowSensorPulseCount = 0;
  //attachInterrupt(flowSensorInterrupt, flowSensorPulseCounter, FALLING);
  attachInterrupt(PIN_INTERRUPT_FLOW_SENSOR, flowSensorPulseCounter, FALLING);

  return flowSensorPulsesPerSecond;
}

/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*\
            function resetWhen30Days
\*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
void resetWhen30Days()
{
  if (millis() > (RESET_UPTIME_TIME))
  {
    // do reset
  }
}

/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*\
            function doRequest
\*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
String doRequest(char reqUri, String reqData) {
  String responseText;

  if (httpClient.connect(REST_SERVICE_URL, REST_SERVICE_PORT)) {  //starts client connection, checks for connection
    Serial.println("connected");

    if (reqData.length()) { // do post request
      httpClient.println((char) "POST" + reqUri + "HTTP/1.1");
      //httpClient.println("Host: checkip.dyndns.com"); // TODO remove if not necessary
      httpClient.println("Content-Type: application/csv;");
      httpClient.println("Content-Length: " + reqData.length());
      httpClient.println();
      httpClient.print(reqData);
    } else { // do get request
      httpClient.println( (char) "GET" +  reqUri + "HTTP/1.1");
      //httpClient.println("Host: checkip.dyndns.com"); // TODO remove if not necessary
      httpClient.println("Connection: close");  //close 1.1 persistent connection  
      httpClient.println(); //end of get request
    }
  } else {
    Serial.println("connection failed"); //error message if no client connect
    Serial.println();
  }

  while (httpClient.connected() && !httpClient.available()) {
    delay(1);
  } //waits for data
  while (httpClient.connected() || httpClient.available()) { //connected or data available
    responseText += httpClient.read(); //places captured byte in readString
  }

  return responseText;
}

/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*\
            function readRequest
\*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
bool readRequest(EthernetClient &client)
{
  bool currentLineIsBlank = true;
  while (client.connected()) {
    if (client.available()) {
      char c = client.read();
      if (c == '\n' && currentLineIsBlank) {
        return true;
      } else if (c == '\n') {
        currentLineIsBlank = true;
      } else if (c != '\r') {
        currentLineIsBlank = false;
      }
    }
  }
  return false;
}

/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*\
            Количество свободной памяти
\*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
int freeRam()
{
  extern int __heap_start, *__brkval;
  int v;
  return (int)&v - (__brkval == 0 ? (int)&__heap_start : (int)__brkval);
}
/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*\
            end
\*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
