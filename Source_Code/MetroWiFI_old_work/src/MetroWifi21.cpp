#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <ESPAsyncWebServer.h>
#include <ESPAsyncTCP.h>
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <string.h>
#include <LittleFS.h>

#define pin_rst 4
#define pin_pwr 5
#define pin_dbg 13
#define pin_read 14 //Botón para realizar una lectura del metro y en boot del módulo RESET de las CFG
#define CONFIGFILE "/config.json"
#define SALVAM "/salva.json"

//ID dispositivo
const char *device_name = "METRO";

bool rst_sistema, leerMetro, spiffsActive = false;
const int mqtt_port = 1883;
int estado;
long ask_sta;
String ID, Kwh, Invert, ask_mqtt;

const char *ota_password = "veneno";
const char *ota_hash = "F81F10E631F3C519D5A44D8DA976FB67";

String cliente_id();
void modo_sta();
void modo_ap();
void mqtt_connection();
void reconnect();
void ask();
void Tx_0();
void Tx_CmdLeer();
void Tx_ACK();
void Tx(byte *sec, byte n);

struct Config
{
  int modo;
  int rst;
  String ssid;
  String passwd;
  int delay_ap;
  String mqtt_svr;
  String mqtt_user;
  String mqtt_pass;
  String mqtt_topic;
  String mqtt_ask;
  float read_i;
};

struct SalvaMetro
{
  String mID;
  String mKwh;
  String mInvert;
};

Config config;
SalvaMetro salva;
AsyncWebSocket ws("/ws");
AsyncEventSource events("/events");
DNSServer dns_server;
AsyncWebServer server(80);

WiFiClient espClient;
PubSubClient clientM(espClient);

void loadConfiguration(const char *filename, Config &config)
{
  File file = LittleFS.open(CONFIGFILE, "r");
  if (!file)
  {
    //Serial.println("Failed to open config file for reading");
    return;
  }
  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, file);
  if (error)
  {
    //Serial.println("Failed to read file, using default configuration");
  }
  config.modo = doc["modo"] | 0;
  config.rst = doc["rst"] | 0;
  config.ssid = doc["ssid"].as<String>();
  config.passwd = doc["passwd"].as<String>();
  config.delay_ap = doc["delay_ap"] | 30;
  config.mqtt_svr = doc["mqtt_svr"].as<String>();
  config.mqtt_user = doc["mqtt_user"].as<String>();
  config.mqtt_pass = doc["mqtt_pass"].as<String>();
  config.mqtt_topic = doc["mqtt_topic"].as<String>();
  config.mqtt_ask = doc["mqtt_ask"].as<String>();
  config.read_i = doc["read_i"] | 0.2;
  file.close();
}

void saveConfiguration(const char *filename, const Config &config)
{
  File file = LittleFS.open(CONFIGFILE, "w");
  StaticJsonDocument<512> doc;
  doc["modo"] = config.modo;
  doc["rst"] = config.rst;
  doc["ssid"] = config.ssid;
  doc["passwd"] = config.passwd;
  doc["delay_ap"] = config.delay_ap;
  doc["mqtt_svr"] = config.mqtt_svr;
  doc["mqtt_user"] = config.mqtt_user;
  doc["mqtt_pass"] = config.mqtt_pass;
  doc["mqtt_topic"] = config.mqtt_topic;
  doc["mqtt_ask"] = config.mqtt_ask;
  doc["read_i"] = config.read_i;
  if (serializeJson(doc, file) == 0)
  {
    //Serial.println(F("Failed to write to file"));
  }
  file.close();
}

void loadMetro(const char *filename, SalvaMetro &salva)
{
  File file = LittleFS.open(SALVAM, "r");
  if (!file)
  {
    //Serial.println("Failed to open config file for reading");
    return;
  }
  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, file);
  if (error)
  {
    //Serial.println("Failed to read file, using default configuration");
  }
  salva.mID = doc["mID"].as<String>();
  salva.mKwh = doc["mKwh"].as<String>();
  salva.mInvert = doc["mInvert"].as<String>();
  file.close();
}

void saveMetro(const char *filename, SalvaMetro &salva)
{
  File file = LittleFS.open(SALVAM, "w");
  if (!file)
  {
    //Serial.println("Failed to open config file for reading");
    return;
  }
  StaticJsonDocument<256> doc;
  doc["mID"] = salva.mID;
  doc["mKwh"] = salva.mKwh;
  doc["mInvert"] = salva.mInvert;
  if (serializeJson(doc, file) == 0)
  {
    //Serial.println(F("Failed to write to file"));
  }
  file.close();
}

String processor(const String &var)
{
  if (var == "MODO")
  {
    String result = String(config.modo);
    return result;
  }
  else if (var == "MODOAP")
  {
    if (config.modo == 0)
    {
      return "selected";
    }
    else
    {
      return "";
    }
  }
  else if (var == "MODOSTA")
  {
    if (config.modo == 1)
    {
      return "selected";
    }
    else
    {
      return "";
    }
  }
  if (var == "RST")
  {
    String result = String(config.rst);
    return result;
  }
  else if (var == "RSTON")
  {
    if (config.rst == 0)
    {
      return "checked";
    }
    else
    {
      return "";
    }
  }
  else if (var == "RSTOFF")
  {
    if (config.rst == 1)
    {
      return "checked";
    }
    else
    {
      return "";
    }
  }
  else if (var == "SSID")
  {
    return config.ssid;
  }
  else if (var == "PASSWD")
  {
    return config.passwd;
  }
  else if (var == "DELAY_AP")
  {
    String result = String(config.delay_ap);
    return result;
  }
  else if (var == "MQTT_SVR")
  {
    String result = String(config.mqtt_svr);
    return result;
  }
  else if (var == "MQTT_USER")
  {
    String result = String(config.mqtt_user);
    return result;
  }
  else if (var == "MQTT_PASS")
  {
    String result = String(config.mqtt_pass);
    return result;
  }
  else if (var == "MQTT_TOPIC")
  {
    String result = String(config.mqtt_topic);
    return result;
  }
  else if (var == "MQTT_ASK")
  {
    String result = String(config.mqtt_ask);
    return result;
  }
  else if (var == "READ_I")
  {
    String result = String(config.read_i);
    return result;
  }
  else if (var == "MID")
  {
    String result = cliente_id();
    return result;
  }
  else if (var == "ID")
  {
    String result = ID;
    return result;
  }
  else if (var == "Kwh")
  {
    String result = Kwh;
    return result;
  }
  else if (var == "Invert")
  {
    String result = Invert;
    return result;
  }
  else if (var == "RSSI")
  {
    String result = String(WiFi.RSSI());
    return result;
  }
  return String();
}

//Configuraciones WiFi
void modo_sta()
{
  WiFi.hostname(cliente_id());
  WiFi.mode(WIFI_STA);
  WiFi.begin(config.ssid, config.passwd);
  int count = 0;
  while (WiFi.status() != WL_CONNECTED)
  {
    count = count + 1;
    delay(300);
    if (count >= config.delay_ap)
    {
      count = 0;
      estado = 0;
      modo_ap();
      break;
    }
  }
}
void modo_ap()
{
  WiFi.mode(WIFI_AP);
  WiFi.hostname(cliente_id());
  WiFi.softAP(cliente_id(), config.passwd, 11, false, 1);
  IPAddress ap_ip = {192, 168, 4, 1};
  IPAddress subnet = {255, 255, 255, 0};
  WiFi.softAPConfig(ap_ip, ap_ip, subnet);
}

//Obtener ID_Chip
String cliente_id()
{
  String temp_x = device_name;
  temp_x += '_';
  temp_x += String(ESP.getChipId());
  char temp_y[temp_x.length()];
  temp_x.toCharArray(temp_y, temp_x.length());
  return temp_y;
}

//Reconectar MQTT
void mqtt_connection()
{
  if (!clientM.connected())
  {
    reconnect();
  }
  clientM.loop();
}

void reconnect()
{
  int count = 0;
  while (!clientM.connected())
  {
    count = count + 1;
    if (count >= 50)
    {
      estado = 2;
      break;
    }
    if (clientM.connect(device_name, config.mqtt_user.c_str(), config.mqtt_pass.c_str()))
    {
      clientM.subscribe(config.mqtt_ask.c_str());
    }
    else
    {
      delay(200);
    }
  }
}

void callback(char *topic, byte *payload, unsigned int length)
{
  payload[length] = '\0';
  ask_mqtt = (char *)payload;
}

//Resetear sistema
void resetsis()
{
  if (rst_sistema)
  {
    digitalWrite(pin_pwr, LOW);
    digitalWrite(pin_dbg, LOW);
    delay(2000);
    for (int i = 0; i < 10; i++)
    {
      digitalWrite(pin_dbg, !digitalRead(pin_dbg));
      delay(200);
    }
    digitalWrite(pin_rst, HIGH);
  }
}

class CaptiveRequestHandler : public AsyncWebHandler
{
public:
  CaptiveRequestHandler() {}
  virtual ~CaptiveRequestHandler() {}
  bool canHandle(AsyncWebServerRequest *request)
  {
    //request->addInterestingHeader("ANY");
    return true;
  }
  void handleRequest(AsyncWebServerRequest *request)
  {
    request->send(LittleFS, "/index.html", String(), false, processor);
  }
};

void setup()
{
  rst_sistema = false;
  leerMetro = false;
  pinMode(pin_read, INPUT);
  pinMode(pin_dbg, OUTPUT);
  pinMode(pin_pwr, OUTPUT);
  pinMode(pin_rst, OUTPUT);
  digitalWrite(pin_dbg, LOW);
  digitalWrite(pin_rst, LOW);
  digitalWrite(pin_pwr, HIGH);
  digitalWrite(pin_dbg, HIGH);
  Serial.begin(1200, SERIAL_7E1);
  Serial1.begin(76923, SERIAL_6N1); //Configuración UART1 (TX1 --> GPIO2)
  if (LittleFS.begin())
  {
    spiffsActive = true;
    loadConfiguration(CONFIGFILE, config);
    loadMetro(SALVAM, salva);
  }
  else
  {
    //Serial.println("Unable to activate SPIFFS");
  }
  delay(3000);
  if (digitalRead(pin_read) && config.rst == 0)
  { //Reset por defecto
    digitalWrite(pin_pwr, LOW);
    digitalWrite(pin_dbg, HIGH);
    config.modo = 0;
    config.rst = 0;
    config.ssid = "METRO";
    config.passwd = "12345678";
    config.delay_ap = 30;
    config.mqtt_svr = "";
    config.mqtt_user = "";
    config.mqtt_pass = "";
    config.mqtt_topic = "";
    config.mqtt_ask = "";
    config.read_i = 0.2;
    saveConfiguration(CONFIGFILE, config);
    salva.mID = "0000";
    salva.mKwh = "0000";
    salva.mInvert = "0000";
    saveMetro(SALVAM, salva);
    delay(2000);
    rst_sistema = true;
  }
  //Chekar en que modo cargar la WiFi
  estado = config.modo;
  if (estado == 0)
  {
    modo_ap();
  }
  if (estado == 1 || estado == 2)
  {
    modo_sta();
  }

  clientM.setServer(config.mqtt_svr.c_str(), mqtt_port);
  clientM.setCallback(callback);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/index.html", String(), false, processor);
  });
  server.on("/ask_env", HTTP_GET, [](AsyncWebServerRequest *request) {
    ask_mqtt = "ask";
    request->redirect("/");
  });
  server.on("/set_config", HTTP_GET, [](AsyncWebServerRequest *request) {
    int config_changed = 0;
    if (request->hasParam("modo"))
    {
      String p = request->getParam("modo")->value();
      int modo = p.toInt();
      config.modo = modo;
      config_changed = 1;
    }
    if (request->hasParam("rst"))
    {
      String p = request->getParam("rst")->value();
      int rst = p.toInt();
      config.rst = rst;
      config_changed = 1;
    }
    if (request->hasParam("ssid"))
    {
      String p = request->getParam("ssid")->value();
      config.ssid = p;
      config_changed = 1;
    }
    if (request->hasParam("passwd"))
    {
      String p = request->getParam("passwd")->value();
      config.passwd = p;
      config_changed = 1;
    }
    if (request->hasParam("delay_ap"))
    {
      String p = request->getParam("delay_ap")->value();
      int delay_ap = p.toInt();
      config.delay_ap = delay_ap;
      config_changed = 1;
    }
    if (request->hasParam("mqtt_svr"))
    {
      String p = request->getParam("mqtt_svr")->value();
      config.mqtt_svr = p;
      config_changed = 1;
    }
    if (request->hasParam("mqtt_user"))
    {
      String p = request->getParam("mqtt_user")->value();
      config.mqtt_user = p;
      config_changed = 1;
    }
    if (request->hasParam("mqtt_pass"))
    {
      String p = request->getParam("mqtt_pass")->value();
      config.mqtt_pass = p;
      config_changed = 1;
    }
    if (request->hasParam("mqtt_topic"))
    {
      String p = request->getParam("mqtt_topic")->value();
      config.mqtt_topic = p;
      config_changed = 1;
    }
    if (request->hasParam("mqtt_ask"))
    {
      String p = request->getParam("mqtt_ask")->value();
      config.mqtt_ask = p;
      config_changed = 1;
    }
    if (request->hasParam("read_i"))
    {
      String p = request->getParam("read_i")->value();
      float read_i = p.toFloat();
      config.read_i = read_i;
      config_changed = 1;
    }
    if (config_changed == 1)
    {
      //Serial.println("Config cambiada, guardando");
      saveConfiguration(CONFIGFILE, config);
      //Serial.println("Config guardada, redireccionando");
      rst_sistema = true;
      request->redirect("/");
    }
    request->send(LittleFS, "/set_config.html", String(), false, processor);
  });
  server.begin();
  ArduinoOTA.setPassword(ota_password);
  ArduinoOTA.setPasswordHash(ota_hash);
  ArduinoOTA.setHostname("METRO");
  ArduinoOTA.begin();
  dns_server.start(53, "*", WiFi.softAPIP());
  server.addHandler(new CaptiveRequestHandler()).setFilter(ON_AP_FILTER);
  server.addHandler(&ws);
  digitalWrite(pin_pwr, LOW);
  digitalWrite(pin_dbg, LOW);
  ID = salva.mID;
  Kwh = salva.mKwh;
  Invert = salva.mInvert;
}

void loop()
{
  ArduinoOTA.handle();
  resetsis();
  dns_server.processNextRequest();
  /*Modo Punto de Acceso
    Módulo funciona como Hotspot.
    Se pueden ver las lecturas del metro:
    -APK Kilowatt
    -Interfaz WEB
  */
  if (estado == 0)
  {
    digitalWrite(pin_pwr, HIGH);
    digitalWrite(pin_dbg, LOW);
    if (WiFi.softAPgetStationNum() >= 1)
    {
      if (millis() - ask_sta > config.read_i * 3.6e+6 || digitalRead(pin_read) || ask_mqtt == "ask")
      {
        leerMetro = true;
        while (leerMetro)
        {
          ask();
          if (!leerMetro)
          {
            ask_mqtt = "";
            ask_sta = millis();
            break;
          }
        }
      }
    }
  }
  /*Modo cliente A
     Módulo conectado a una red WiFi Local con Broker MQTT.
     Se pueden ver las lecturas del metro:
     -APK Kilowatt (Pendiente)
     -Interfaz WEB
     -MQTT
  */
  if (estado == 1)
  {
    digitalWrite(pin_pwr, LOW);
    digitalWrite(pin_dbg, HIGH);
    mqtt_connection();
    if ((millis() - ask_sta > config.read_i * 3.6e+6) || digitalRead(pin_read) || ask_mqtt == "ask")
    {
      leerMetro = true;
      while (leerMetro)
      {
        ask();
        if (!leerMetro)
        {
          mqtt_connection();
          StaticJsonDocument<256> doc;
          doc["mid"] = cliente_id();
          doc["id"] = ID;
          doc["kwh"] = Kwh;
          doc["invert"] = Invert;
          doc["rssi"] = String(WiFi.RSSI());
          //Creamos un String para rellenarlo con el JSON
          char buffer[256];
          //Serializamos el JSON como String
          serializeJson(doc, buffer);
          //Publicamos el JSON
          clientM.publish(String(config.mqtt_topic).c_str(), buffer, true);
          ask_mqtt = "";
          ask_sta = millis();
          break;
        }
      }
    }
  }
  /*Modo cliente B
    Módulo conectado a una red WiFi Local sin Broker MQTT.
    Se pueden ver las lecturas del metro:
    -APK Kilowatt (Pendiente)
    -Interfaz WEB
  */
  if (estado == 2)
  {
    digitalWrite(pin_pwr, LOW);
    analogWrite(pin_dbg, 90);
    if (millis() - ask_sta > config.read_i * 3.6e+6 || digitalRead(!pin_read) || ask_mqtt == "ask")
    {
      leerMetro = true;
      while (leerMetro)
      {
        ask();
        if (!leerMetro)
        {
          ask_mqtt = "";
          ask_sta = millis();
          break;
        }
      }
    }
  }
}

void ask()
{
  //char ACK[] = {0x06, 0x30, 0x32, 0x30, 0x0d, 0x0a};
  boolean found = false;
  String tmp_ID = "";
  String tmp_Kwh = "";
  String tmp_Invert = "";
  Serial.flush();
  //------------------------------------------
  Tx_CmdLeer(); //Serial.println("/?!");
  //------------------------------------------
  String line = Serial.readStringUntil('\n');
  while (line && line.length() > 0)
  {
    resetsis();
    if (line.equals("/ZTY2ZT\r"))
      found = true;
    line = Serial.readStringUntil('\n');
  }
  if (found)
  {
    Serial.flush();
    //-------------------------------------------
    Tx_ACK(); //Serial.write(ACK, 6);
    //-------------------------------------------
    line = Serial.readStringUntil('\n');
    while (line && line.length() > 0)
    {
      int start = line.indexOf("96.1.0(");
      if (start != -1)
      {
        start += 7;
        int end = line.indexOf(")", start);
        if (end != -1 && end - start == 12)
        {
          tmp_ID = line.substring(start, end);
        }
      }
      else
      {
        int start = line.indexOf("1.8.0(");
        if (start != -1)
        {
          start += 6;
          int end = line.indexOf("*kWh)", start);
          if (end != -1 && end - start == 9)
          {
            tmp_Kwh = line.substring(start, end);
          }
        }
        else
        {
          int start = line.indexOf("2.8.0(");
          if (start != -1)
          {
            start += 6;
            int end = line.indexOf("*kWh)", start);
            if (end != -1 && end - start == 9)
            {
              tmp_Invert = line.substring(start, end);
            }
          }
        }
      }
      line = Serial.readStringUntil('\n');
    }
    if (tmp_ID.length() > 0 && tmp_Kwh.length() > 0 && tmp_Invert.length() > 0)
    {
      ID = tmp_ID;
      Kwh = tmp_Kwh;
      Invert = tmp_Invert;
      salva.mID = ID;
      salva.mKwh = Kwh;
      salva.mInvert = Invert;
      saveMetro(SALVAM, salva);
      leerMetro = false;
    }
  }
}

void Tx_0()
{
  byte ir38[] = {0x15, 0x15, 0x15, 0x15, 0x15, 0x15, 0x15, 0x15};
  Serial1.write(ir38, 8);
  Serial1.flush();
}
//
void Tx_CmdLeer()
{ //Ya incluye el bit de paridad
  byte sec1[] = {0xAF, 0x3F, 0x21, 0x8D, 0x0A};
  Tx(sec1, 5);
}
//
void Tx_ACK()
{ //Ya incluye el bit de paridad
  byte sec2[] = {0x06, 0x30, 0xB2, 0x30, 0x8D, 0x0A};
  Tx(sec2, 6);
}
//
void Tx(byte *sec, byte n)
{
  for (byte j = 0; j < n; j++)
  {
    Tx_0(); //StartBit del byte
    for (byte i = 0; i < 8; i++)
    { // Cada bit del byte
      if (bitRead(*sec, i))
        delayMicroseconds(832);
      else
        Tx_0();
    }
    delayMicroseconds(832); //StopBit del byte
    sec++;                  //Próximo byte
  }
}