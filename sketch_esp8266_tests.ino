#include <ESP8266WebServer.h>

#include <ESP8266WiFi.h>


IPAddress local_IP(192,168,1,1);
IPAddress gateway(192,168,1,1);
IPAddress subnet(255,255,255,0);

const char* SSID = "TestAP";
const char* password = "test";


ESP8266WebServer server(80);



void handleRoot()
{
  server.send(200, "text/html", "<h1>Something is working! /sg</h1>");
}

void setup()
{
  Serial.begin(115200);
  Serial.println();

  Serial.print("Setting soft-AP configuration ... ");
  boolean softApConfigSuccess = WiFi.softAPConfig(local_IP, gateway, subnet);
  Serial.println( softApConfigSuccess ? "Ready" : "Failed!");

  Serial.print("Setting soft-AP ... ");
  boolean softApStartSuccess = WiFi.softAP("TESTAP", "testtest");
  Serial.println( softApStartSuccess ? "Ready" : "Failed");

  server.on("/", handleRoot);
  server.begin();
  Serial.print("Server listening on");
  Serial.println(WiFi.softAPIP());
}

void loop()
{
  Serial.printf("Stations connected = %d\n", WiFi.softAPgetStationNum());
  delay(3000);
  server.handleClient(); 
}
