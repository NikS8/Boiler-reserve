/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*\
                                                    boilerBack.ino 
                                Copyright © 2018, Zigfred & Nik.S
19.12.2018 v1
03.01.2019 v2 dell <ArduinoJson.h>
10.01.2019 v3 изменен расчет в YF-B5
13.01.2019 v4 createDataString в формате json
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

#define DEVICE_ID 'boilerBack'
#define VERSION '4'

#define RESET_UPTIME_TIME 2592000000  //  =30 * 24 * 60 * 60 * 1000 // reset after 30 days uptime
#define REST_SERVICE_URL "192.168.1.210"
#define REST_SERVICE_PORT 3010
char settingsServiceUri[] = "/settings/boilerBack";
char intervalLogServiceUri[] = "/intervalLog/boilerBack";

// settings
byte mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED};
EthernetServer httpServer(40250);
EthernetClient httpClient;

EnergyMonitor emon1;
EnergyMonitor emon2;
EnergyMonitor emon3;

#define PIN_ONE_WIRE_BUS 9
#define DS18_CONVERSION_TIME 750 / (1 << (12 - ds18Precision))
unsigned short ds18DeviceCount;
bool isDS18ParasitePowerModeOn;
uint8_t ds18Precision = 9;
OneWire ds18wireBus(PIN_ONE_WIRE_BUS);
DallasTemperature ds18Sensors(&ds18wireBus);

byte flowSensorInterrupt = 0; // 0 = digital pin 2
byte flowSensorPin = 2;
unsigned long flowSensorLastTime = 0;
volatile long flowSensorPulseCount = 0;

// time
unsigned long currentTime;
//unsigned long flowSensorLastTime;
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

  pinMode( A1, INPUT );
  pinMode( A2, INPUT );
  pinMode( A3, INPUT );

  emon1.current(1, 8.4);
  emon2.current(2, 8.4);
  emon3.current(3, 8.4);

  pinMode(flowSensorPin, INPUT);
  digitalWrite(flowSensorPin, HIGH);
  attachInterrupt(flowSensorInterrupt, flowSensorPulseCounter, FALLING);
  sei();

  httpServer.begin();

  ds18Sensors.begin();
  ds18DeviceCount = ds18Sensors.getDeviceCount();
  isDS18ParasitePowerModeOn = ds18Sensors.isParasitePowerMode();
  ds18Sensors.setWaitForConversion(false);

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
  ds18Sensors.requestTemperatures();
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

  reqClient.println("HTTP/1.1 200 OK");
  reqClient.println("Content-Type: application/json");
  reqClient.println("Content-Length: " + data.length());
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
    ds18Sensors.requestTemperatures();
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

  uint8_t index;
  DeviceAddress deviceAddress;
  ds18Sensors.getAddress(deviceAddress, index);
  index = 0;

  resultData.concat("{");
  resultData.concat("\n\"deviceId\":");
  resultData.concat(DEVICE_ID);
  resultData.concat(",");
  resultData.concat("\n\"version\":");
  resultData.concat(VERSION);
  resultData.concat(",");
  resultData.concat("\n\"data\": {");
  resultData.concat("\n\t\"flow-0\":" + String(getFlowData()));
  resultData.concat(",");
  resultData.concat("\n\t\"trans\": {");
  resultData.concat("\n\t\"trans-1\":" + String(emon1.calcIrms(1480)));
  resultData.concat(",");
  resultData.concat("\n\t\"trans-2\":" + String(emon2.calcIrms(1480)));
  resultData.concat(",");
  resultData.concat("\n\t\"trans-3\":" + String(emon3.calcIrms(1480)));
  resultData.concat("\n\t}");
  resultData.concat(",");
  resultData.concat("\n\t\"ds18\": {");
  resultData.concat("\n\t\"");
  resultData.concat(String(dsAddressToString(deviceAddress)) + "\":" + String(ds18Sensors.getTempC(deviceAddress)));
  for (index = 1; index < ds18DeviceCount; index++)
  {
    DeviceAddress deviceAddress;
    ds18Sensors.getAddress(deviceAddress, index);
    resultData.concat(",");
    resultData.concat("\n\t\"");
    resultData.concat(String(dsAddressToString(deviceAddress)) + "\":" + String(ds18Sensors.getTempC(deviceAddress)));
  }
  resultData.concat("\n\t }");
  resultData.concat("\n\t }");
  resultData.concat("\n}");

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

  detachInterrupt(flowSensorInterrupt);
  //     flowSensorPulsesPerSecond = (1000 * flowSensorPulseCount / (millis() - flowSensorLastTime));
  //    flowSensorPulsesPerSecond = (flowSensorPulseCount * 1000 / deltaTime);
  flowSensorPulsesPerSecond = flowSensorPulseCount;
  flowSensorPulsesPerSecond *= 1000;
  flowSensorPulsesPerSecond /= deltaTime; //  количество за секунду

  flowSensorLastTime = millis();
  flowSensorPulseCount = 0;
  attachInterrupt(flowSensorInterrupt, flowSensorPulseCounter, FALLING);

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
            function dsAddressToString
\*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
String dsAddressToString(DeviceAddress deviceAddress)
{
  String address;
  for (uint8_t i = 0; i < 8; i++) {
    if (deviceAddress[i] < 16 ) address += "0";
    address += String(deviceAddress[i], HEX);
  }
  return address;
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
            the end
\*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/