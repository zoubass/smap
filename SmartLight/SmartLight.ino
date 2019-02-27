#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Wire.h>
#include <BH1750.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>

//SWITCHES
boolean recordStats = true;
boolean autoLight = true;
boolean lightSwitchedOn = true;
boolean detectMotion = false;
long delay_after_motion = 5000;

int numberOfSavedDays = 0;
int numberOfSavedHours = 0;

struct LightingData {
  String day;
  String dutyHour[24];
};

struct Stats {
  LightingData dbStats[2];
};

LightingData actualDayData;
Stats stats;

//JSON

int recordedHour = 1;
String recordedDay = "1";

float model[17] = {0.6, 0.7, 0.6, 0.5, 0.5, 0.4, 0.2, 0, 0, 0.1, 0.4, 0.8, 0.9, 1, 0.6, 0.4, 0};

const char *ssid = "ukacko";
const char *password = "lfhkmedici2016";

WebServer server(80);

uint16_t lux        = 250;
int maxLux     = 32;
BH1750 lightMeter(0x23);

#define LEDC_CHANNEL_0_R  0
#define LEDC_CHANNEL_1_G  1
#define LEDC_CHANNEL_2_B  2

#define LEDC_TIMER_13_BIT  8

#define LEDC_BASE_FREQ  5000

#define LED_PIN_R   19
#define LED_PIN_G   18
#define LED_PIN_B   17

#define MOTION_SENSOR 34
// TIME STUFF

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);
String timeStamp;
int duty = 256;
// pasmo ve kterem je pouzivano
int GTM = 1;
String formattedDate;
String dayStamp;

void handleNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";

  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }

  server.send(404, "text/plain", message);
}

String serialize() {
  String json;
  StaticJsonBuffer<500> jsonBuffer;
  for (int i = 0; i < numberOfSavedDays; i++) {
    JsonObject& root = jsonBuffer.createObject();
    root["day"] = stats.dbStats[i].day;
    JsonArray& array = root.createNestedArray("data");

    for (int j = 0; j < numberOfSavedHours; j++) {
      array.add(stats.dbStats[i].dutyHour[j]);
    }
    String jsonTemp;
    root.prettyPrintTo(jsonTemp);
    Serial.println(jsonTemp);

    json = json + jsonTemp;
    jsonTemp = "";
  }
  return json;
}

void setup(void) {
  Wire.begin();

  if (lightMeter.begin()) {
    Serial.println(F("BH1750 initialised"));
  }
  else {
    Serial.println(F("Error initialising BH1750"));
  }

  pinMode(MOTION_SENSOR, INPUT);

  ledcSetup(LEDC_CHANNEL_0_R, LEDC_BASE_FREQ, LEDC_TIMER_13_BIT);
  ledcAttachPin(LED_PIN_R, LEDC_CHANNEL_0_R);

  ledcSetup(LEDC_CHANNEL_1_G, LEDC_BASE_FREQ, LEDC_TIMER_13_BIT);
  ledcAttachPin(LED_PIN_G, LEDC_CHANNEL_1_G);

  ledcSetup(LEDC_CHANNEL_2_B, LEDC_BASE_FREQ, LEDC_TIMER_13_BIT);
  ledcAttachPin(LED_PIN_B, LEDC_CHANNEL_2_B);

  ledcWrite(LEDC_CHANNEL_0_R, duty);
  ledcWrite(LEDC_CHANNEL_1_G, duty);
  ledcWrite(LEDC_CHANNEL_2_B, duty);

  Serial.begin(115200);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.println("");

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  if (MDNS.begin("esp32")) {
    Serial.println("MDNS responder started");
  }

  server.on("/led/on", []() {
    lightSwitchedOn = true;
    ledcWrite(LEDC_CHANNEL_0_R, 256);
    ledcWrite(LEDC_CHANNEL_1_G, 256);
    ledcWrite(LEDC_CHANNEL_2_B, 256);
    server.send(200, "text/plain", "LED is ON!");
  });

  server.on("/beacon", []() {
    server.send(200, "text/plain", "HELL0_THERE");
  });

  server.on("/led/off", []() {
    lightSwitchedOn = false;
    ledcWrite(LEDC_CHANNEL_0_R, 0);
    ledcWrite(LEDC_CHANNEL_1_G, 0);
    ledcWrite(LEDC_CHANNEL_2_B, 0);
    server.send(200, "text/plain", "LED is OFF!");
  });

  server.on("/led", []() {
    if (server.arg("duty") != "") {
      //TODO: Aplikuj jako transition z predesle duty
      int duty = server.arg("duty").toInt();
      ledcWrite(LEDC_CHANNEL_0_R, duty);
      ledcWrite(LEDC_CHANNEL_1_G, duty);
      ledcWrite(LEDC_CHANNEL_2_B, duty);
      server.send(200, "text/plain", "Applied duty.");
    } else if (server.arg("red") != "") {
      int red = server.arg("red").toInt();
      int green = server.arg("green").toInt();
      int blue = server.arg("blue").toInt();

      ledcWrite(LEDC_CHANNEL_0_R, red);
      ledcWrite(LEDC_CHANNEL_1_G, green);
      ledcWrite(LEDC_CHANNEL_2_B, blue);
      server.send(200, "text/plain", "Applied color.");
    } else {
      server.send(200, "text/plain", "Duty parameter not specified");
    }
  });

  server.on("/detectMotion/on", []() {
    Serial.println("Starting to detect Motion");
    detectMotion = true;
    if (server.arg("duration") != "") {
      delay_after_motion = server.arg("duration").toInt();
    }
    server.send(200, "text/plain", "Motion detecting is ON!");
  });

  server.on("/detectMotion/off", []() {
    Serial.println("Starting to detect Motion");
    detectMotion = false;
    server.send(200, "text/plain", "Motion detecting is OFF!");
  });

  server.on("/stats", []() {
    server.send(200, "application/json", serialize());
  });

  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("HTTP server started");

  timeClient.begin();
  timeClient.setTimeOffset(3600 * GTM);
}

boolean motionDetected = false;
long detectTime = 0;
void loop(void) {

  while (!timeClient.update()) {
    timeClient.forceUpdate();
  }

  server.handleClient();
  lux = lightMeter.readLightLevel();

  if (lightSwitchedOn) {
    if (detectMotion) {
      long motionState = digitalRead(MOTION_SENSOR);
      if (motionState == HIGH) {
        duty = 256;
        ledcWrite(LEDC_CHANNEL_0_R, duty);
        ledcWrite(LEDC_CHANNEL_1_G, duty);
        ledcWrite(LEDC_CHANNEL_2_B, duty);
        motionDetected = true;
        //Serial.println("motionDetected");
        detectTime = millis();
      }
    }


    if (motionDetected) {
      if ((millis() - detectTime)  > delay_after_motion) {
        motionDetected = false;
        detectTime = 0;
      }
    } else {
      autoBrightness();
    }
  }
  delay(300);
}

int getHour() {
  formattedDate = timeClient.getFormattedDate();

  int splitT = formattedDate.indexOf("T");
  dayStamp = formattedDate.substring(0, splitT);

  // Extract time
  timeStamp = formattedDate.substring(splitT + 1, formattedDate.length() - 1);
  return atoi( timeStamp.c_str() );
}

void autoBrightness() {
  float actualLux = lux;
  int actualHour = getHour();
  //Serial.println("AutoBrightness");
  float dayTimePercentage = model[actualHour - 7];

  int actualDuty;

  if (actualHour > 22 || actualHour < 7) {
    actualDuty = 0;
    Serial.println("--------------SWITCHED OFF-----------");
  } else {
    int actualDutyWTime = (((1 - (actualLux / maxLux)) + dayTimePercentage) / 2) * 256;
    actualDuty = (1 - (actualLux / maxLux)) * 256;
    //Serial.println("Actual duty");
    //Serial.println(actualDuty);
    actualDuty = actualDutyWTime;

    if (actualDuty < 0) {
      actualDuty = 0;
    }
  }

  transition(actualDuty);
  duty = actualDuty;


  if (recordStats) {
    //JSON stats
    if (!recordedDay.equals(dayStamp)) {
      numberOfSavedDays += 1;
      recordedDay = dayStamp;
      actualDayData = { formattedDate, {""}};
    }

    if (recordedHour != actualHour) {
      recordedHour = actualHour;
      numberOfSavedHours += 1;
      Serial.println("Another hour recorded!");
      Serial.println(recordedHour);
      actualDayData.dutyHour[numberOfSavedHours - 1] = String(duty) + "-" + String(actualHour);
      stats.dbStats[numberOfSavedDays - 1] = actualDayData;
    }
    Serial.println(stats.dbStats[0].dutyHour[0]);
    Serial.println(stats.dbStats[0].dutyHour[1]);
    if (actualHour == 0) {
      if (numberOfSavedDays < 2) {
        stats.dbStats[numberOfSavedDays - 1] = actualDayData;
      } else {
        numberOfSavedDays = 0;
        numberOfSavedHours = 0;
        actualDayData = { "", {""}};
        stats.dbStats[0] = actualDayData;
      }
    }
  }

}

void transition(int toDuty) {
  if (toDuty > duty ) {
    for (int i = duty; i <= toDuty; i++) {
      ledcWrite(LEDC_CHANNEL_0_R, i);
      ledcWrite(LEDC_CHANNEL_1_G, i);
      ledcWrite(LEDC_CHANNEL_2_B, i);
      delay(10);
    }
  } else {
    for (int i = duty; i >= toDuty ; i--) {
      ledcWrite(LEDC_CHANNEL_0_R, i);
      ledcWrite(LEDC_CHANNEL_1_G, i);
      ledcWrite(LEDC_CHANNEL_2_B, i);
      delay(10);
    }
  }
}
