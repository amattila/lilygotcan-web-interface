/*
  FSWebServer - Example WebServer with SPIFFS backend for esp8266
  Copyright (c) 2015 Hristo Gochkov. All rights reserved.
  This file is part of the ESP8266WebServer library for Arduino environment.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.
  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.
  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

  upload the contents of the data folder with MkSPIFFS Tool ("ESP8266 Sketch Data Upload" in Tools menu in Arduino IDE)
  or you can upload the contents of a folder if you CD in that folder and run the following command:
  for file in `ls -A1`; do curl -F "file=@$PWD/$file" esp8266fs.local/edit; done

  access the sample web page at http://esp8266fs.local
  edit the page by going to http://esp8266fs.local/edit
*/
/*
 * This file is part of the esp32 web interface
 *
 * Copyright (C) 2023 Johannes Huebner <dev@johanneshuebner.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */
#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <HTTPUpdateServer.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <FS.h>
#include <Ticker.h>
#include <StreamString.h>
#include "oi_can.h"

#include <SPI.h>
#include <SD.h>

#define DBG_OUTPUT_PORT Serial
#define LED_BUILTIN  5

#define RESERVED_SD_SPACE 2000000000
#define SDIO_BUFFER_SIZE 16384
#define FLUSH_WRITES 60 //flush file every 60 blocks

#define MAX_SD_FILES 200

#define SD_MISO_PIN 2
#define SD_MOSI_PIN 15
#define SD_SCLK_PIN 14
#define SD_CS_PIN 13

#define LOG_DELAY_VAL 10000

const char* host = "inverter";

// Helper function to check if string contains only digits
bool isDigitsOnly(const String& str) {
  for (size_t i = 0; i < str.length(); i++) {
    if (!isDigit(str.charAt(i))) {
      return false;
    }
  }
  return true;
}
char jsonFileName[50];
//DynamicJsonDocument jsonDoc(30000);

WebServer server(80);
HTTPUpdateServer updater;
//holds the current upload
File fsUploadFile;
Ticker sta_tick;

bool haveSDCard = false;


//format bytes
String formatBytes(uint64_t bytes){
  if (bytes < 1024){
    return String(bytes)+"B";
  } else if(bytes < (1024 * 1024)){
    return String(bytes/1024.0)+"KB";
  } else if(bytes < (1024 * 1024 * 1024)){
    return String(bytes/1024.0/1024.0)+"MB";
  } else {
    return String(bytes/1024.0/1024.0/1024.0)+"GB";
  }
}

String getContentType(String filename){
  if(server.hasArg("download")) return "application/octet-stream";
  else if(filename.endsWith(".bin")) return "application/octet-stream";
  else if(filename.endsWith(".htm")) return "text/html";
  else if(filename.endsWith(".html")) return "text/html";
  else if(filename.endsWith(".css")) return "text/css";
  else if(filename.endsWith(".js")) return "application/javascript";
  else if(filename.endsWith(".png")) return "image/png";
  else if(filename.endsWith(".gif")) return "image/gif";
  else if(filename.endsWith(".jpg")) return "image/jpeg";
  else if(filename.endsWith(".ico")) return "image/x-icon";
  else if(filename.endsWith(".xml")) return "text/xml";
  else if(filename.endsWith(".pdf")) return "application/x-pdf";
  else if(filename.endsWith(".zip")) return "application/x-zip";
  else if(filename.endsWith(".gz")) return "application/x-gzip";
  return "text/plain";
}

bool handleFileRead(String path){
  //DBG_OUTPUT_PORT.println("handleFileRead: " + path);
  if(path.endsWith("/")) path += "index.html";
  String contentType = getContentType(path);
  String pathWithGz = path + ".gz";
  if(SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)){
    if(SPIFFS.exists(pathWithGz))
      path += ".gz";
    File file = SPIFFS.open(path, "r");
    if(!file) {
      return false;
    }
    server.sendHeader("Cache-Control", "max-age=86400");
    size_t sent = server.streamFile(file, contentType);
    file.close();
    return true;
  }
  //try download from the sdcard
  if (haveSDCard) {
    DBG_OUTPUT_PORT.print("handleFileRead Trying SD Card: ");
    DBG_OUTPUT_PORT.println(path);

    File file = SD.open(path);
    if (!file) {
      return false;
    }
    size_t sent = server.streamFile(file, contentType);
    file.close();
    return true;
  }
  return false;
}

void handleFileUpload(){
  if(server.uri() != "/edit") return;
  HTTPUpload& upload = server.upload();
  if(upload.status == UPLOAD_FILE_START){
    String filename = upload.filename;
    if(!filename.startsWith("/")) filename = "/"+filename;
    //DBG_OUTPUT_PORT.print("handleFileUpload Name: "); DBG_OUTPUT_PORT.println(filename);
    fsUploadFile = SPIFFS.open(filename, "w");
    filename = String();
  } else if(upload.status == UPLOAD_FILE_WRITE){
    //DBG_OUTPUT_PORT.print("handleFileUpload Data: "); DBG_OUTPUT_PORT.println(upload.currentSize);
    if(fsUploadFile)
      fsUploadFile.write(upload.buf, upload.currentSize);
  } else if(upload.status == UPLOAD_FILE_END){
    if(fsUploadFile)
      fsUploadFile.close();
    //DBG_OUTPUT_PORT.print("handleFileUpload Size: "); DBG_OUTPUT_PORT.println(upload.totalSize);
  }
}

void handleFileDelete(){
  if(server.args() == 0) return server.send(500, "text/plain", "BAD ARGS");
  String path = server.arg(0);
  String storage = server.arg(1);

  //DBG_OUTPUT_PORT.println("handleFileDelete: " + path);
  if(path == "/")
    return server.send(500, "text/plain", "BAD PATH");

  if (storage == "onboard") {
    if(!SPIFFS.exists(path))
      return server.send(404, "text/plain", "FileNotFound");
    SPIFFS.remove(path);
  } else if (storage == "sdcard") {
    SD.remove(path);
  }

  server.send(200, "text/plain", "");
  path = String();
}

void handleFileCreate(){
  if(server.args() == 0)
    return server.send(500, "text/plain", "BAD ARGS");
  String path = server.arg(0);
  //DBG_OUTPUT_PORT.println("handleFileCreate: " + path);
  if(path == "/")
    return server.send(500, "text/plain", "BAD PATH");
  if(SPIFFS.exists(path))
    return server.send(500, "text/plain", "FILE EXISTS");
  File file = SPIFFS.open(path, "w");
  if(!file)
    return server.send(500, "text/plain", "CREATE FAILED");
  file.close();
  server.send(200, "text/plain", "");
  path = String();
}

void handleSdCardList() {

  if (!haveSDCard) {
    server.send(200, "text/json", "{\"error\": \"No SD Card\"}");
    return;
  }
  File root = SD.open("/");
  if(!root){
    server.send(200, "text/json", "{\"error\": \"Failed to open directory\"}");
    return;
  }
  if(!root.isDirectory()){
    server.send(200, "text/json", "{\"error\": \"Root is not a directory\"}");
    return;
  }
  String output = "[";

  File file = root.openNextFile();
  while(file){
    if (output != "[") output += ',';
    bool isDir = false;
    output += "{\"type\":\"";
    output += file.isDirectory()?"dir":"file";
    output += "\",\"name\":\"";
    output += String(file.name());
    output += "\"}";
    file = root.openNextFile();
  }

  output += "]";
  server.send(200, "text/json", output);
}


void handleFileList() {
  String path = "/";
  if(server.hasArg("dir"))
    path = server.arg("dir");
  //DBG_OUTPUT_PORT.println("handleFileList: " + path);
  File root = SPIFFS.open(path);
  String output = "[";

  if(!root){
    //DBG_OUTPUT_PORT.print("- failed to open directory");
    return;
  }

  File file = root.openNextFile();
  while(file){
    if (output != "[") output += ',';
    bool isDir = false;
    output += "{\"type\":\"";
    output += file.isDirectory()?"dir":"file";
    output += "\",\"name\":\"";
    output += String(file.name());
    output += "\"}";
    file = root.openNextFile();
  }

  output += "]";
  server.send(200, "text/json", output);
}


static void handleCommand() {
  const int cmdBufSize = 128;
  if(!server.hasArg("cmd")) {server.send(500, "text/plain", "BAD ARGS"); return;}

  String cmd = server.arg("cmd");

  digitalWrite(LED_BUILTIN, HIGH);

  if (cmd == "json") {
    if (!OICan::SendJson(server.client())) {
      server.send(500, "text/plain", "CAN communication error");
      return;
    }
  }
  else if (cmd.startsWith("set")) {
    String str(cmd);
    int nameStart = str.indexOf(' ');
    int valueStart = str.lastIndexOf(' ');
    int endPos = str.indexOf('\r');

    if (nameStart == -1 || valueStart == -1 || endPos == -1 || nameStart >= valueStart || valueStart >= endPos) {
      server.send(400, "text/plain", "Invalid set command format");
      return;
    }

    String name = str.substring(nameStart, valueStart);
    String valueStr = str.substring(valueStart, endPos);
    valueStr.trim();
    double value = valueStr.toDouble();
    name.trim();

    switch (OICan::SetValue(name, value)) {
      case OICan::Ok:
        server.send(200, "text/plain", "Set Ok");
        break;
      case OICan::UnknownIndex:
        server.send(200, "text/plain", "Unknown Parameter");
        break;
      case OICan::ValueOutOfRange:
        server.send(200, "text/plain", "Value out of range");
        break;
      case OICan::CommError:
        server.send(200, "text/plain", "CAN communication error");
        break;
    }
  }
  else if (cmd.startsWith("stream")) {
    String str(cmd);
    int samplesStart = str.indexOf(' ');
    int namesStart = str.lastIndexOf(' ');
    int endPos = str.indexOf('\r');

    if (samplesStart == -1 || namesStart == -1 || endPos == -1 || samplesStart >= namesStart || namesStart >= endPos) {
      server.send(400, "text/plain", "Invalid stream command format");
      return;
    }

    String samplesStr = str.substring(samplesStart, namesStart);
    samplesStr.trim();
    int samples = samplesStr.toInt();

    String names = str.substring(namesStart, endPos);
    String result = OICan::StreamValues(names, samples);

    server.send(200, "text/plain", result);
  }
  else if (cmd == "save") {
    if (OICan::SaveToFlash()) {
      server.send(200, "text/plain", "Parameters and CAN map saved");
    }
    else {
      server.send(200, "text/plain", "No reply to save command");
    }
  }

  digitalWrite(LED_BUILTIN, LOW);
}

static void handleCanMap() {
  digitalWrite(LED_BUILTIN, HIGH);
  OICan::SetResult res = OICan::Ok;

  if (server.hasArg("add")) {
    res = OICan::AddCanMapping(server.arg("add"));
  }
  else if (server.hasArg("remove")) {
    res = OICan::RemoveCanMapping(server.arg("remove"));
  }
  else if (server.hasArg("edit")) {
    res = OICan::RemoveCanMapping(server.arg("edit"));
    if (res == OICan::Ok)
      res = OICan::AddCanMapping(server.arg("edit"));
  }

  if (res == OICan::Ok)
    OICan::SendCanMapping(server.client());
  else if (res == OICan::CommError)
    server.send(500, "text/plain", "CAN communication error");
  else if (res == OICan::UnknownIndex)
    server.send(500, "text/plain", "Invalid request");
  digitalWrite(LED_BUILTIN, LOW);
}

static void handleUpdate()
{
  static int pages = 0;
  if(!server.hasArg("step") || !server.hasArg("file")) {server.send(500, "text/plain", "BAD ARGS"); return;}

  String stepStr = server.arg("step");
  if(stepStr.length() == 0 || !isDigitsOnly(stepStr)) {
    server.send(400, "text/plain", "Invalid step parameter");
    return;
  }
  int step = stepStr.toInt();
  String message;
  digitalWrite(LED_BUILTIN, HIGH);

  if (step < 0)
    pages = OICan::StartUpdate(server.arg("file"));
  else {
    while (OICan::GetCurrentUpdatePage() < step) {
      OICan::Loop();
    }
  }

  server.send(200, "text/json", "{ \"message\": \"" + message + "\", \"pages\": " + pages + " }");
  digitalWrite(LED_BUILTIN, LOW);
}

static void handleNodeId()
{
  if(server.hasArg("id") && server.hasArg("canspeed")) {
    String idStr = server.arg("id");
    String speedStr = server.arg("canspeed");

    if(!isDigitsOnly(idStr) || !isDigitsOnly(speedStr)) {
      server.send(400, "text/plain", "Invalid parameters");
      return;
    }

    int id = idStr.toInt();
    int speed = speedStr.toInt();

    if(speed < 0 || speed > 2) {
      server.send(400, "text/plain", "Invalid CAN speed");
      return;
    }

    OICan::BaudRate baud = speed == 0 ? OICan::Baud125k : (speed == 1 ? OICan::Baud250k : OICan::Baud500k);
    OICan::Init(id, baud);
  }
  else if(server.hasArg("id")) {
    String idStr = server.arg("id");
    if(!isDigitsOnly(idStr)) {
      server.send(400, "text/plain", "Invalid ID parameter");
      return;
    }
    int id = idStr.toInt();
    OICan::Init(id, OICan::Baud500k);
  }

  server.send(200, "text/plain", String(OICan::GetNodeId()) + "," + String(OICan::GetBaudRate()));
}

static void handleWifi()
{
  bool updated = true;
  if(server.hasArg("apSSID") && server.hasArg("apPW"))
  {
    WiFi.softAP(server.arg("apSSID").c_str(), server.arg("apPW").c_str());
  }
  else if(server.hasArg("staSSID") && server.hasArg("staPW"))
  {
    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(server.arg("staSSID").c_str(), server.arg("staPW").c_str());
  }
  else
  {
    File file = SPIFFS.open("/wifi.html", "r");
    if(!file) {
      server.send(500, "text/plain", "Failed to load WiFi settings");
      return;
    }
    String html = file.readString();
    file.close();
    html.replace("%staSSID%", WiFi.SSID());
    html.replace("%apSSID%", WiFi.softAPSSID());
    html.replace("%staIP%", WiFi.localIP().toString());
    server.send(200, "text/html", html);
    updated = false;
  }

  if (updated)
  {
    File file = SPIFFS.open("/wifi-updated.html", "r");
    if(file) {
      size_t sent = server.streamFile(file, getContentType("wifi-updated.html"));
      file.close();
    }
  }
}



void staCheck(){
  sta_tick.detach();
  if(!(uint32_t)WiFi.localIP()){
    WiFi.mode(WIFI_AP); //disable station mode
  }
}


void setup(void){
  DBG_OUTPUT_PORT.begin(115200);

  pinMode(LED_BUILTIN, OUTPUT);


  //Start SPI Flash file system
  if (!SPIFFS.begin(true)) {
    DBG_OUTPUT_PORT.println("SPIFFS Mount Failed! Formatting...");
    if (SPIFFS.format()) {
      DBG_OUTPUT_PORT.println("SPIFFS formatted successfully");
      if (SPIFFS.begin()) {
        DBG_OUTPUT_PORT.println("SPIFFS mounted after formatting");
      } else {
        DBG_OUTPUT_PORT.println("SPIFFS still failed to mount after formatting");
      }
    } else {
      DBG_OUTPUT_PORT.println("SPIFFS formatting failed!");
    }
  } else {
    DBG_OUTPUT_PORT.println("SPIFFS mounted successfully");
  }

  //WIFI INIT
  #ifdef WIFI_IS_OFF_AT_BOOT
    enableWiFiAtBootTime();
  #endif

  DBG_OUTPUT_PORT.println("Initializing WiFi...");
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);

  // Give WiFi some time to initialize
  delay(1000);

  // Try to connect to saved WiFi if available
  WiFi.begin();
  delay(2000);

  sta_tick.attach(10, staCheck);

  DBG_OUTPUT_PORT.println("WiFi initialization complete");

  MDNS.begin(host);

  OICan::Init(1, OICan::Baud500k);

  updater.setup(&server);

  //SERVER INIT
  ArduinoOTA.setHostname(host);
  ArduinoOTA.begin();
  //list directory
  server.on("/list", HTTP_GET, handleFileList);
  server.on("/sdcard/list", HTTP_GET, handleSdCardList);


  //load editor
  server.on("/edit", HTTP_GET, [](){
    if(!handleFileRead("/edit.htm")) server.send(404, "text/plain", "FileNotFound");
  });
  //create file
  server.on("/edit", HTTP_PUT, handleFileCreate);
  //delete file
  server.on("/edit", HTTP_DELETE, handleFileDelete);
  //first callback is called after the request has ended with all parsed arguments
  //second callback handles file uploads at that location
  server.on("/edit", HTTP_POST, [](){ server.send(200, "text/plain", ""); }, handleFileUpload);

  server.on("/wifi", handleWifi);
  server.on("/cmd", handleCommand);
  server.on("/canmap", handleCanMap);
  server.on("/fwupdate", handleUpdate);
  server.on("/version", [](){ server.send(200, "text/plain", "1.1.R"); });
  server.on("/nodeid", handleNodeId);
  server.on("/reboot", HTTP_GET, [](){

    server.send(200, "text/plain", "Rebooting");
    OICan::DeleteParams();
    ESP.restart();
  });
  //called when the url is not defined here
  //use it to load content from SPIFFS
  server.onNotFound([](){
    if(!handleFileRead(server.uri()))
    {
      server.sendHeader("Refresh", "6; url=/update");
      server.send(404, "text/plain", "FileNotFound");
    }
  });

  server.begin();
  // Note: setNoDelay should be called on active client connections, not here
  // server.client().setNoDelay(1);  // Removed - causes "Bad file number" error

  MDNS.addService("http", "tcp", 80);

  SPI.begin(SD_SCLK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  if (!SD.begin(SD_CS_PIN, SPI)) {
    DBG_OUTPUT_PORT.println("SDCard MOUNT FAIL");
  } else {
    uint32_t cardSize = SD.cardSize() / (1024 * 1024);
    String str = "SDCard Size: " + String(cardSize) + "MB";
    haveSDCard = true;
    DBG_OUTPUT_PORT.println(str);
  }

}

void loop(void){
  static int subIndex = 0;
  static uint32_t serial[4];
  // note: ArduinoOTA.handle() calls MDNS.update();
  server.handleClient();
  ArduinoOTA.handle();

  OICan::Loop();
}
