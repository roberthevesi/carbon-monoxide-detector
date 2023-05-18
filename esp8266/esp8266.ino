#include "arduino_secrets.h"

//////////////////////////////// DISCORD ////////////////////////////////

// #include <Discord_WebHook.h>
// Discord_Webhook discord; // Create a Discord_Webhook object

// String DISCORD_WEBHOOK = SECRET_WEBHOOK;
#include <string>
#include <WiFiClientSecure.h>
WiFiClientSecure discord_wifi_client;

//////////////////////////////// AWS ////////////////////////////////

#include <ESP8266WiFi.h>
#include <MySQL_Connection.h>
#include <MySQL_Cursor.h>

// AWS RDS credentials
IPAddress server_addr(13, 53, 202, 210);

// MySQL connection and cursor objects
WiFiClient client;
MySQL_Connection conn((Client *)&client);
MySQL_Cursor cur(&conn);

//////////////////////////////// TIME ////////////////////////////////

#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
WiFiClient client_api_time;

//////////////////////////////// BUZZER ////////////////////////////////

#include <Ticker.h>

const int buzzer_pin = D0;
int buzz_counter = 3;         // Number of buzzes per round
int buzz_time = 500;          // Buzz time in milliseconds
float buzz_intensity = 0.25;  // Buzzer intensity between 0 and 1
Ticker buzzerTicker;

//////////////////////////////// MQ9 ////////////////////////////////

#define THRESHOLD 5
// 70-200 warning, you might wanna check this
// 200+ DANGER, EVACUATE

#include <vector>
std::vector<float> co_level_vector;
#define MAX_VECTOR_SIZE 5

#include <MQUnifiedsensor.h>
/************************Hardware Related Macros************************************/
#define Board ("ESP8266")
#define Pin (A0)  //Analog input 4 of your arduino
/***********************Software Related Macros************************************/
#define Type ("MQ-9")  //MQ9
#define Voltage_Resolution (3.3)
#define ADC_Bit_Resolution (10)   // For arduino UNO/MEGA/NANO
#define RatioMQ9CleanAir (9.6)    //RS / R0 = 60 ppm
#define PreaheatControlPin5 (3)   // Preaheat pin to control with 5 volts
#define PreaheatControlPin14 (4)  // Preaheat pin to control with 1.4 volts
/*****************************Globals***********************************************/
//Declare Sensor
MQUnifiedsensor MQ9(Board, Voltage_Resolution, ADC_Bit_Resolution, Pin, Type);

//////////////////////////////// SETUP ////////////////////////////////
void setup() {
  Serial.begin(9600);
  delay(10);

  pinMode(buzzer_pin, OUTPUT);

  // Connect to Wi-Fi
  WiFi.begin(SECRET_WIFI_SSID, SECRET_WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi");

  // discord
  discord_wifi_client.setInsecure();

  // Connect to AWS RDS
  Serial.println("Connecting to AWS RDS...");
  if (conn.connect(server_addr, SECRET_SERVER_PORT, SECRET_DB_USER, SECRET_DB_PASS, SECRET_DB_NAME)) {
    Serial.println("Connected to AWS RDS");
  } else {
    Serial.println("Connection to AWS RDS failed");
  }

  // MQ9
  //Init the serial port communication - to debug the library
  Serial.begin(9600);  //Init serial port
  pinMode(PreaheatControlPin5, OUTPUT);
  pinMode(PreaheatControlPin14, OUTPUT);

  //Set math model to calculate the PPM concentration and the value of constants
  MQ9.setRegressionMethod(1);  //_PPM =  a*ratio^b
  MQ9.setA(1000.5);
  MQ9.setB(-2.186);  // Configure the equation to to calculate LPG concentration

  MQ9.init();

  /*****************************  MQ CAlibration ********************************************/
  Serial.println("Preheating MQ9 sensor, please wait 2.5 minutes");
  digitalWrite(PreaheatControlPin5, HIGH);
  digitalWrite(PreaheatControlPin14, LOW);
  delay(600);
  digitalWrite(PreaheatControlPin5, LOW);
  digitalWrite(PreaheatControlPin14, HIGH);
  delay(900);
  digitalWrite(PreaheatControlPin5, HIGH);
  digitalWrite(PreaheatControlPin14, LOW);
  delay(600);
  digitalWrite(PreaheatControlPin5, LOW);
  digitalWrite(PreaheatControlPin14, HIGH);
  delay(900);
  digitalWrite(PreaheatControlPin5, HIGH);
  digitalWrite(PreaheatControlPin14, LOW);

  Serial.print("Calibrating, please wait...");
  float calcR0 = 0;
  for (int i = 1; i <= 10; i++) {
    MQ9.update();
    calcR0 += MQ9.calibrate(RatioMQ9CleanAir);
    Serial.print(".");
  }
  MQ9.setR0(calcR0 / 10);
  Serial.println("  done!.");

  if (isinf(calcR0)) {
    Serial.println("Warning: Conection issue, R0 is infinite (Open circuit detected) please check your wiring and supply");
    while (1)
      ;
  }

  if (calcR0 == 0) {
    Serial.println("Warning: Conection issue found, R0 is zero (Analog pin shorts to ground) please check your wiring and supply");
    while (1)
      ;
  }

  MQ9.serialDebug(true);
}

//////////////////////////////// LOOP ////////////////////////////////
void loop() {
  // init
  digitalWrite(PreaheatControlPin5, LOW);
  digitalWrite(PreaheatControlPin14, HIGH);

  // get co_level
  float co_level = get_current_co_level();

  // get date and time
  String date_time = get_current_datetime();
  char *date_time_char = new char[date_time.length() + 1];
  strcpy(date_time_char, date_time.c_str());

  // check dangerous flag
  bool dangerous = co_level >= THRESHOLD ? 1 : 0;

  // send data
  send_data_to_database(co_level, date_time_char, dangerous);

  // char query[128];
  // snprintf(query, sizeof(query), "(%.2f, '%s', %d)", co_level, date_time_char, dangerous);
  // discord.send(query);
  // query[0] = '\0';

  add_co_level_to_vector(co_level);
  if (dangerous) {
    if (co_level_vector.size() == MAX_VECTOR_SIZE)
      check_co_level_readings(date_time);
  }


  delay(5000);
}

void send_data_to_discord(float co_level, String date_time) {
  if (WiFi.status() == WL_CONNECTED) {
    String content;
    if (co_level < (THRESHOLD + 100))
      content = "Hey, your Carbon Monoxide levels at " + date_time + " were quite high, measuring " + String(co_level) + " ppm. You should take measures, like opening the windows ASAP!";
    else
      content = "WARNING! Your Carbon Monoxide levels at " + date_time + " were REALLY high, measuring " + String(co_level) + " ppm. You should get out and call Emergencies ASAP!";

    HTTPClient http;
    http.begin(discord_wifi_client, SECRET_WEBHOOK);
    http.addHeader("Content-Type", "application/json");  // Set request as JSON

    String tts = "false";
    int httpCode = http.POST("{\"content\":\"" + content + "\",\"tts\":" + tts + "}");
    http.end();
  }
}

void check_co_level_readings(String date_time) {
  float min_reading = co_level_vector[0];

  for (int i = 1; i < co_level_vector.size(); i++)
    if (co_level_vector[i] < min_reading)
      min_reading = co_level_vector[i];

  if (min_reading >= THRESHOLD && min_reading < (THRESHOLD + 100)) {
    // send to disc
    send_data_to_discord(co_level_vector.back(), date_time);

    buzz_intensity = 0.25;
    buzz();
  } else if (min_reading >= (THRESHOLD + 100)) {
    // send to disc
    send_data_to_discord(co_level_vector.back(), date_time);

    buzz_intensity = 1;
    buzz();
    delay(2000);
    buzz();
  }
}

void add_co_level_to_vector(float co_level) {
  if (co_level_vector.size() < MAX_VECTOR_SIZE) {
    co_level_vector.push_back(co_level);
  } else {
    co_level_vector.erase(co_level_vector.begin());
    co_level_vector.push_back(co_level);
  }
}

void buzz() {
  for (int i = 0; i < buzz_counter; i++) {
    analogWrite(buzzer_pin, buzz_intensity * 255);  // Set buzzer intensity (PWM)
    delay(buzz_time);
    analogWrite(buzzer_pin, 0);  // Turn off buzzer
    delay(buzz_time);
  }
}

float get_current_co_level() {
  digitalWrite(PreaheatControlPin5, HIGH);
  digitalWrite(PreaheatControlPin14, LOW);

  MQ9.update();      // Update data, the arduino will read the voltage from the analog pin
  MQ9.readSensor();  // Sensor will read PPM concentration using the model, a and b values set previously or from the setup
  // MQ9.serialDebug(); // print the table on the serial port

  float ppm = MQ9.getPPM();

  digitalWrite(PreaheatControlPin5, LOW);
  digitalWrite(PreaheatControlPin14, HIGH);

  return ppm;
}

String get_current_datetime() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;

    http.begin(client_api_time, "http://ip-api.com/json");  // Specify request destination for getting timezone
    int httpCode = http.GET();

    if (httpCode > 0) {
      String payload = http.getString();
      DynamicJsonDocument doc(1024);
      deserializeJson(doc, payload);

      String timezone = doc["timezone"];  // Get the timezone
      http.end();                         // Close the connection

      // Get the current time
      http.begin(client_api_time, "http://worldtimeapi.org/api/timezone/" + timezone);  // Specify request destination for getting time
      httpCode = http.GET();

      if (httpCode > 0) {
        payload = http.getString();
        deserializeJson(doc, payload);

        String datetime = doc["datetime"];             // Get the current date and time
        String datePart = datetime.substring(0, 10);   // "2023-05-11"
        String timePart = datetime.substring(11, 19);  // "18:35:14"

        // Combine date and time parts into a new string
        String mysqlDateTime = datePart + " " + timePart;  // "2023-05-11 18:35:14"
        return mysqlDateTime;
      }
    }
    http.end();  //Close connection
  }

  return "-1";
}

void send_data_to_database(float co_level, char *date_time, bool dangerous) {
  MySQL_Cursor *cur_mem = new MySQL_Cursor(&conn);
  char query[128];

  snprintf(query, sizeof(query), "INSERT INTO readings (co_level, date_time, dangerous) VALUES (%.2f, '%s', %d)", co_level, date_time, dangerous);
  Serial.println(query);
  cur_mem->execute(query);
  delete cur_mem;
}
