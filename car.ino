/*
Created by: Mateusz Suszczyk
Contact: mateusz@suszczyk.pl
Date: 11.02.2023 r.
Language: c++/arduino
Developed for: ESP8266 WIFI development board (NodeMCU v2)
Intended for use with: L298N H-Bridge, 4 dc motors, Temperature sensor DHT 11, PIR sensor HC-SR501, Sound sensor

// After uploading code to ESP8266, upload index.html to ESP using command below:
// curl -F "file=@index.html" 192.168.90.200/upload
*/

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <FS.h> //SPI flash file system library using to load website into flash memory
#include <WebSocketsServer.h>
#include <ArduinoJson.h>

// Temeperature sensor
#include <DHT.h>
#define DHTPIN 2
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// Initialization variables
float humidity = 0.0;
float temperature = 0.0;
float noise = 0.0;

// Assign pin numbers for L298N enable and inputs
int in1 = 4;
int in2 = 0;
int in3 = 14;
int in4 = 12;

// Variables for updating sensors
long sensorUpdateFrequency = 50;
long timeNow = 0;
long timePrev = 0;

// Assign pin numbers for PIR and sound sensor
int pir_sensor = 5;
int noise_sensor = 13;
int sampleBufferValue = 0; // Variable to compute noise

ESP8266WebServer server;
WebSocketsServer webSocket = WebSocketsServer(81);

// Network credentials
char *ssid = "******";
char *password = "******";
// Hold uploaded file
File fsUploadFile;

void setup()
{
  SPIFFS.begin();
  WiFi.begin(ssid, password);
  Serial.begin(115200);
  dht.begin();

  pinMode(pir_sensor, INPUT);
  pinMode(noise_sensor, INPUT);

  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print(".");
    delay(500);
  }
  Serial.println("");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  server.on("/", ControlDataFile);

  // List available files
  server.on("/list", HTTP_GET, FileList);

  // Handle file upload
  server.on(
      "/upload", HTTP_POST, []()
      { server.send(200, "text/plain", "{\"success\":1}"); },
      FileUpload);

  server.onNotFound([]()
                    {
    if(!FileRead(server.uri())) {
      server.send(404, "text/plain", "File Not Found!");
    } });

  server.begin();
  webSocket.begin();
  // Function to be called whenever there is a websocket event
  webSocket.onEvent(webSocketEvent);

  // Motor outputs
  pinMode(in1, OUTPUT);
  pinMode(in2, OUTPUT);
  pinMode(in3, OUTPUT);
  pinMode(in4, OUTPUT);
}

void loop()
{
  // Check the websocket status
  webSocket.loop();
  server.handleClient();
  int test_movement = digitalRead(pir_sensor); // Get PIR sensor value

  // See if it is time to update the sensor values on the website
  timeNow = millis();
  if (digitalRead(noise_sensor) == LOW)
  {
    sampleBufferValue++;
  }

  if (timeNow - timePrev >= sensorUpdateFrequency)
  {
    // Serial.println(sampleBufferValue);
    int noise = sampleBufferValue / 5;
    // Serial.print("Sensor value: "); Serial.println(test_movement);
    // Serial.print("Noise value: "); Serial.println(noise);
    sampleBufferValue = 0;
    timePrev = timeNow;
    // If it is time, call the updateSensors() function
    updateSensors(noise, test_movement);
  }
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length)
{
  if (type == WStype_TEXT)
  {
    // Handle the websocket messages with direction and speed
    // by parsing the parameters from a JSON string
    String payload_str = String((char *)payload);
    // Using the ArduinoJson library
    StaticJsonDocument<200> doc;
    // Deserialize the data
    DeserializationError error = deserializeJson(doc, payload_str);
    // Parse the parameters we expect to receive (TO-DO: error handling)
    String dir = doc["direction"];
    Serial.println(dir);
    if (dir == "STP")
    {
      // Motors
      digitalWrite(in1, LOW);
      digitalWrite(in2, LOW);
      digitalWrite(in3, LOW);
      digitalWrite(in4, LOW);
      Serial.println("STOP");
    }
    else
    {
      if (dir == "FWD")
      {
        Serial.println("FORWARD");
        digitalWrite(in1, HIGH);
        digitalWrite(in2, LOW);
        digitalWrite(in3, HIGH);
        digitalWrite(in4, LOW);
      }
      else if (dir == "BWD")
      {
        Serial.println("BACKWARD");
        digitalWrite(in1, LOW);
        digitalWrite(in2, HIGH);
        digitalWrite(in3, LOW);
        digitalWrite(in4, HIGH);
      }
      else if (dir == "RGT")
      {
        Serial.println("RIGHT");
        digitalWrite(in1, LOW);
        digitalWrite(in2, HIGH);
        digitalWrite(in3, HIGH);
        digitalWrite(in4, LOW);
      }
      else if (dir == "LFT")
      {
        Serial.println("LEFT");
        digitalWrite(in1, HIGH);
        digitalWrite(in2, LOW);
        digitalWrite(in3, LOW);
        digitalWrite(in4, HIGH);
      }
    }
  }
}

void FileUpload()
{
  // Deal with data file upload over httpupload
  HTTPUpload &upload = server.upload();
  if (upload.status == UPLOAD_FILE_START)
  {
    String filename = upload.filename;
    if (!filename.startsWith("/"))
    {
      filename = "/" + filename;
    }
    Serial.print("FileUpload Name: ");
    Serial.println(filename);
    fsUploadFile = SPIFFS.open(filename, "w");
  }
  else if (upload.status == UPLOAD_FILE_WRITE)
  {
    if (fsUploadFile)
    {
      fsUploadFile.write(upload.buf, upload.currentSize);
    }
  }
  else if (upload.status == UPLOAD_FILE_END)
  {
    if (fsUploadFile)
    {
      fsUploadFile.close();
    }
    Serial.print("FileUpload Size: ");
    Serial.println(upload.totalSize);
  }
}
void FileList()
{
  // Updates list of html pages for controling the car
  String path = "/";
  // Assuming there are no subdirectories
  Dir dir = SPIFFS.openDir(path);
  String output = "[";
  while (dir.next())
  {
    File entry = dir.openFile("r");
    // Separate by comma if there are multiple files
    if (output != "[")
      output += ",";
    output += String(entry.name()).substring(1);
    entry.close();
  }
  output += "]";
  server.send(200, "text/plain", output);
}

void ControlDataFile()
{
  // Initiates html page on server to control the car
  File file = SPIFFS.open("/index.html", "r");
  server.streamFile(file, "text/html");
  file.close();
}

String getContentType(String filename)
{
  if (filename.endsWith(".html"))
    return "text/html";
  else if (filename.endsWith(".js"))
    return "text/javascript";
  return "text/plain";
}

bool FileRead(String path)
{
  // Serve index file when top root path is accessed
  if (path.endsWith("/"))
    path += "index.html";
  // Different file types require different actions
  String contentType = getContentType(path);

  if (SPIFFS.exists(path))
  {
    fs::File file = SPIFFS.open(path, "r");
    if (contentType == "text/plain" || "text/javascript")
      server.streamFile(file, contentType);
    // Display the file on the client's browser

    file.close();
    return true;
  }
  return false; // If the file doesn't exist or can't be opened
}

void updateSensors(int noise, int movement)
{
  float humidity = dht.readHumidity();
  // Serial.println(humidity);
  float temperature = dht.readTemperature();
  // Serial.println(temperature);

  // If any value is isnan (not a number) then there is an error
  if (isnan(humidity) || isnan(temperature))
  {
    Serial.println("Error reading from the DHT11.");
  }
  else
  {
    String json = "{\"temperature\":";
    json += temperature;
    json += ",\"humidity\":";
    json += humidity;
    json += ",\"movement\":";
    json += movement;
    json += ",\"noise\":";
    json += noise;
    json += "}";

    Serial.println(json); // DEBUGGING
    webSocket.broadcastTXT(json.c_str(), json.length());
  }
}