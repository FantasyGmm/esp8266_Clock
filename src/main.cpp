#include <Arduino.h>
#include <U8g2lib.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <html.h>
#include <Arduino_JSON.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <FS.h>
#include <LittleFS.h>
#include <time.h>
#include <DNSServer.h>

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/U8X8_PIN_NONE);
ESP8266WebServer server(80);
DNSServer dnsServer;
WiFiUDP udp_server;
int screen_sleep = 0;
unsigned long otime = 0;
unsigned long dtime = 0;
struct tm localTime;
bool bNeedUpdate = false;
bool bNeedinit = true;
struct WifiData
{
    String ssid;
};
struct Weather
{
  ulong time;
  char city[20];
  char wea[20];
  char tem[4];
  char dtem[4];
  char ntem[4];
  char win[8];
  char air[4];
  Weather()
  {
    this->time = 0;
  }
};
Weather weather;
/*
//I2C
     OLED  ---  ESP8266
     VCC   ---  3.3V / 5V
     GND   ---  G (GND)
     SCL   ---  D1(GPIO5)
     SDA   ---  D2(GPIO4)
*/
void drawXBM(uint8_t width, uint8_t height, uint8_t *bmp)
{
  u8g2.setDrawColor(0);
  u8g2.setBitmapMode(false);
  u8g2.clearBuffer();
  u8g2.drawXBM(0, 0, width, height, bmp);
  u8g2.sendBuffer();
}

void drawProgress(int progress, String caption)
{
  u8g2.setFont(u8g2_font_wqy12_t_gb2312a);
  u8g2.setDrawColor(1);
  u8g2.firstPage();
  do
  {
    u8g2.drawUTF8(0, 12, caption.c_str());
    u8g2.drawRFrame(11, 26, 101, 20, 2);
    u8g2.drawBox(12, 26, progress, 19);

  } while(u8g2.nextPage());
}
bool readConfig(String path, String key, char* value)
{
  File configFile = LittleFS.open(path, "r");
  if (!configFile)
    return false; 
  String json = configFile.readString();
  JSONVar jo = JSON.parse(json);
  configFile.close();
  if (JSON.typeof(jo) == "undefined")
  {
    return false;
  }
  if (!jo.hasOwnProperty(key))
  {
    Serial.printf("Failed to Read Config Key:%s", key.c_str());
    return false;
  }
  strcpy(value, jo[key]);
  return true;
}
bool getJson(String path, char* buf)
{
  File configFile = LittleFS.open(path, "r");
  if (!configFile)
    return false;
  strcpy(buf, configFile.readString().c_str());
  configFile.close();
  return true;
}
bool writeConfig(String path, String key, String value)
{
  JSONVar jo;
  File configFile;
  if (LittleFS.exists(path))
  {
    configFile = LittleFS.open(path, "r+");
    String json = configFile.readString();
    jo = JSON.parse(json);
  }
  else
  {
    configFile = LittleFS.open(path, "w+");
  }
  jo[key] = value;
  configFile.seek(0);
  String json = JSON.stringify(jo);
  configFile.write(json.c_str());
  configFile.close();
  return true;
}
void initdAP()
{
  IPAddress  APip(192,168,1,1);
  IPAddress Subnet(255,255,255,0);
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(APip,APip,Subnet);
  String APname = ("ESP8266_"+(String)ESP.getChipId());
  WiFi.softAP(APname.c_str());
  dnsServer.start(53,"*",APip);
}

int scanWiFi(WifiData* wdata, int len)
{
  int n = WiFi.scanNetworks();
  if (n > 0)
  {
    len = n > len ? len : n; 
    for (int i = 0; i < len; i++)
    {
      wdata[i].ssid = WiFi.SSID(i);
    }
  }
  return len;
}

void connectWiFi(String wifi_ssid,String wifi_pw)
{
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(wifi_ssid, wifi_pw);
  int timeout = 0;
  while (WiFi.status() != WL_CONNECTED) 
  {
    delay(500);
    if (timeout > 20)
    {
      WiFi.disconnect(false);
      initdAP();
      break;
    }
    timeout++;
  }
}

void handleinit()
{
server.send(200,"text/html",initPage);
}

void  handleRoot()
{
  if (bNeedinit)
  {
    handleinit();
  }
  else
  {
    server.send(200,"text/html",indexPage);
  }
}
void handleAPI()
{
  if (server.hasArg("scan_wifi"))
  {
    WifiData* wdata = new WifiData[15];
    int n = scanWiFi(wdata, 15);
    if (n <= 0)
    {
      server.send(200, "application/json", "{\"code\":1,\"msg\":\"No WiFi Detected!\"}");
      return;
    }
    JSONVar jo;
    jo["code"] = 0;
    jo["msg"] = "Scan Completed!";
    JSONVar ja;
    for(int i = 0; i < n; i++)
    {
      JSONVar ji;
      ji["ssid"] = (wdata + i)->ssid;
      ja[i] = ji;
    }
    jo["data"] = ja;
    String json;
    json = JSON.stringify(jo);
    server.send(200, "application/json", json.c_str());
    delete[] wdata;
  }
  else if (server.hasArg("get_config"))
  {
    char* buf = new char[512];
    if (!getJson("/config.json", buf))
    {
      server.send(200, "application/json", "{\"code\":1,\"msg\":\"get config error!\"}");
      return;
    }  
    JSONVar jo;
    JSONVar jd = JSON.parse(buf);
    jo["code"] = 0;
    jo["msg"] = "OK";
    jo["data"] = jd;
    server.send(200, "application/json", JSON.stringify(jo));
    delete[] buf;
  }
  else
    server.send(405, "text/html", "Method Not Allowed");
}

void handleForm()
{
  if (server.method() != HTTP_POST)
  {
    server.send(405, "text/html", "Method Not Allowed");
  }else
  {
    const String method =  server.arg("method");
    if (method == "init_config")
    {
      if (!server.hasArg("wifi_ssid") || !server.hasArg("wifi_password"))
        server.send(405, "text/html", "Method Not Allowed");

      if (!(writeConfig("/config.json", "wifi_ssid", server.arg("wifi_ssid")) && writeConfig("/config.json", "wifi_password", server.arg("wifi_password"))))
      {
        server.send(200, "application/json", "{\"code\":1,\"msg\":\"Write Config Error!\"}");
        return;
      }
      server.send(200, "application/json", "{\"code\":0,\"msg\":\"Config Saved, Esp8266 Will Be Restart!\"}");
      delay(500);
      ESP.restart();
    }else if (method == "save_config")
    {
      if (!server.hasArg("wifi_ssid") || !server.hasArg("wifi_password") || !server.hasArg("weather_appid") || !server.hasArg("weather_appsecret"))
        server.send(405, "text/html", "Method Not Allowed");

      if (server.hasArg("weather_city"))
      {
        writeConfig("/config.json", "weather_city", server.arg("weather_city"));
      }
      if (!(writeConfig("/config.json", "wifi_ssid", server.arg("wifi_ssid")) && writeConfig("/config.json", "wifi_password", server.arg("wifi_password")) && writeConfig("/config.json", "weather_appid", server.arg("weather_appid")) && writeConfig("/config.json", "weather_appsecret", server.arg("weather_appsecret"))))
      {
        server.send(200, "application/json", "{\"code\":1,\"msg\":\"Write Config Error!\"}");
        return;
      }
      server.send(200, "application/json", "{\"code\":0,\"msg\":\"Config Saved, Esp8266 Will Be Restart!\"}");
      delay(1000);
      ESP.restart();
    }else if (method == "reboot")
    {
      server.send(200, "application/json", "{\"code\":0,\"msg\":\"Esp8266 Will Be Restart!\"}");
      delay(1000);
      ESP.restart();
    }else if (method == "reset")
    {
      LittleFS.format();
      server.send(200, "application/json", "{\"code\":0,\"msg\":\"Format Completed, Esp8266 Will Be Restart!\"}");
      delay(1000);
      ESP.restart();
    }else if (method =="screen")
    {
      if (screen_sleep)
      {
        u8g2.setPowerSave(0);
        screen_sleep = 0;
        server.send(200, "text/html", "");
      }else
      {
        u8g2.setPowerSave(1);
        screen_sleep = 1;
        server.send(200, "text/html", "");
      }
    }else if(method == "md5")
      {
        server.send(200,"text/html",ESP.getSketchMD5());
      }else
    {
      server.send(405, "text/html", "Method Not Allowed");
    }
  }
}

void handleNotFound() 
{
  if (bNeedinit)
  {
    handleinit();
    return;
  }
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++)
  {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/html", message);
}

bool getWeather()
{
  char appid[16];
  char appsecret[16];
  char city[16];
  char weatherUrl[128];
  WiFiClient client;
  HTTPClient http;
  String json = "";
  if (!(readConfig("/config.json", "weather_appid", appid) && readConfig("/config.json", "weather_appsecret", appsecret)))
  {
    Serial.println("Weather read config error!");
    return false;
  }
  readConfig("/config.json", "weather_city", city);
  if(strlen(city) > 0)
    sprintf(weatherUrl, "http://tianqiapi.com/api?version=v6&appid=%s&appsecret=%s&city=%s", appid, appsecret, city);
  else
    sprintf(weatherUrl, "http://tianqiapi.com/api?version=v6&appid=%s&appsecret=%s", appid, appsecret);
    if (http.begin(client, weatherUrl)) 
    {
      int httpCode = http.GET();
      if (httpCode > 0)
      {
        if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY)
        {
          json = http.getString();
          JSONVar jo = JSON.parse(json);
          if (JSON.typeof(jo) != "undefined")
          {
            if (jo.hasOwnProperty("errcode"))
              return false  ;
            if (jo["city"] == null || jo["wea"] == null || jo["tem"] == null)
              return false;
            strcpy(weather.city, jo["city"]);
            strcpy(weather.wea, jo["wea"]);
            strcpy(weather.tem, jo["tem"]);
            strcpy(weather.dtem, jo["tem_day"]);
            strcpy(weather.ntem, jo["tem_night"]);
            strcpy(weather.win, jo["win"]);
            strcpy(weather.air, jo["air"]);
            weather.time = millis();
          }
        }
      }
      else
      {
        Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
        return false;
      }
      http.end();
      return true;
    } 
    else 
    {
      Serial.printf("[HTTP} Unable to connect\n");
      return false;
    }
    return false;
}

void getNtpTime()
{
  long timezone = 8;
  byte daysavetime = 0;
  configTime(3600 * timezone, daysavetime * 3600, "ntp.aliyun.com", "time1.cloud.tencent.com");
}
bool getLocalTime()
{
  time_t now;
  time(&now);
  localtime_r(&now, &localTime);
  if (localTime.tm_year > (2016 - 1900))
  {
    return true;
  }
  return false;
}

void drawWeather()
{
  u8g2.firstPage();
  u8g2.setFont(u8g2_font_wqy12_t_gb2312a);
  do{
    u8g2.drawUTF8(14,12,weather.city);
    u8g2.drawUTF8(0,25,"现在温度:");
    u8g2.drawStr(50,25,weather.tem);
    u8g2.drawUTF8(0,38,"白天温度:");
    u8g2.drawStr(0,38,weather.dtem);
    u8g2.drawUTF8(0,51,"夜晚温度:");
    u8g2.drawStr(0,51,weather.ntem);
    u8g2.sendBuffer();
  }while (u8g2.nextPage());
}

void drawWatch()
{
  u8g2.firstPage();
  u8g2.setFont(u8g2_font_wqy12_t_gb2312a);
  do{
    char *nowdate = new char[32];
    char *nowtime = new char[32];
     //获取时间
    sprintf(nowdate, "%d-%02d-%02d", localTime.tm_year + 1900, localTime.tm_mon + 1, localTime.tm_mday);
    sprintf(nowtime, " %02d:%02d:%02d", localTime.tm_hour, localTime.tm_min, localTime.tm_sec);
    switch(localTime.tm_wday)
    {
      case 0:
        u8g2.drawUTF8(74, 50, "星期日");
      break;
      case 1:
        u8g2.drawUTF8(74, 50, "星期一");
      break;
      case 2:
        u8g2.drawUTF8(74, 50, "星期二");
      break;
      case 3:
        u8g2.drawUTF8(74, 50, "星期三");
      break;
      case 4:
        u8g2.drawUTF8(74, 50, "星期四");
      break;
      case 5:
        u8g2.drawUTF8(74, 50, "星期五");
      break;
      case 6:
        u8g2.drawUTF8(74, 50, "星期六");
      break;
    }
    //日期
    u8g2.drawUTF8(5, 50, nowdate);
    //时间
    u8g2.setFont(u8g2_font_fub20_tn);
    u8g2.drawStr(-5, 36, nowtime);
    u8g2.sendBuffer();
    //释放资源
    delete[] nowdate;
    delete[] nowtime;
  }while(u8g2.nextPage());
}

void update_started()
{
  Serial.println("CALLBACK:  HTTP update process started");
}

void update_finished()
{
  Serial.println("CALLBACK:  HTTP update process finished");
}

void update_progress(int cur, int total)
{
  int progress = round((float)cur / total * 100);
  drawProgress(progress, "正在升级,请勿断电!");
}

void update_error(int err)
{
  Serial.printf("CALLBACK:  HTTP update fatal error code %d\n", err);
}
void httpOTA(IPAddress updateAddr)
{
    ESPhttpUpdate.setLedPin(LED_BUILTIN, LOW);
    ESPhttpUpdate.onStart(update_started);
    ESPhttpUpdate.onEnd(update_finished);
    ESPhttpUpdate.onProgress(update_progress);
    ESPhttpUpdate.onError(update_error);
    t_httpUpdate_return ret = ESPhttpUpdate.update("http://" + updateAddr.toString() + ":8266");
    switch (ret) 
    {
      case HTTP_UPDATE_FAILED:
        Serial.printf("HTTP_UPDATE_FAILD Error (%d): %s\n", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
        break;
      case HTTP_UPDATE_NO_UPDATES:
        Serial.println("HTTP_UPDATE_NO_UPDATES");
        break;
      case HTTP_UPDATE_OK:
        Serial.println("HTTP_UPDATE_OK");
        break;
    }
}
void udpServer()
{
  int packetSize = udp_server.parsePacket();
  char rxBuffer[1];
  if (packetSize)
  {
    switch ((int)rxBuffer[0])
    {
    case 0x0:
      for (size_t i = 0; i < 5; i++)
      {
        udp_server.beginPacket(udp_server.remoteIP(),udp_server.remotePort());
        udp_server.write(0x1);
        udp_server.endPacket();
      }
      break;
    case 0x2:
      udp_server.beginPacket(udp_server.remoteIP(),udp_server.remotePort());
      udp_server.write(0x3);
      udp_server.endPacket();
      delay(500);
      bNeedUpdate = true;
    break;
    default:
      break;
    }
  }
}
void setup() {
  u8g2.begin();
  u8g2.enableUTF8Print();
  u8g2.setDrawColor(1);
  Serial.begin(115200);
  LittleFS.begin();
  char wifi_ssid[32],wifi_pw[16];
  bool bssid = readConfig("/config.json", "wifi_ssid", wifi_ssid);
  bool bpass = readConfig("/config.json", "wifi_password", wifi_pw);
  bNeedinit = !(bssid && bpass);
  u8g2.setFont(u8g2_font_wqy12_t_gb2312a);
  u8g2.drawStr(0,13,"Screen Is Online");
  u8g2.sendBuffer();
  delay(500);
  u8g2.drawStr(0,26,"Serial Is Online");
  u8g2.sendBuffer();
  delay(500);
  u8g2.drawStr(0,39,"LittleFS Is Online");
  u8g2.sendBuffer();
  delay(500);
  if(bNeedinit)
  {
    initdAP();
    udp_server.begin(8266);
    u8g2.drawStr(0,52,"AP Is Online");
    u8g2.sendBuffer();
  }else
  {
    connectWiFi(wifi_ssid,wifi_pw);
    udp_server.begin(8266);
    u8g2.drawStr(0,52,"WiFi Is Online");
    u8g2.sendBuffer();
  }
  localTime.tm_year = 0;
  getNtpTime();
  getWeather();
  server.on("/", handleRoot);  
  server.on("/init",handleinit);
  server.on("/api",handleAPI);
  server.on("/form",handleForm);
  server.onNotFound(handleNotFound);
  server.begin();
  delay(800);
  u8g2.clearBuffer();
  u8g2.drawStr(0,13,"Udp Is Online");
  u8g2.sendBuffer();
  delay(500);
  u8g2.drawStr(0,26,"Http Is Online");
  u8g2.sendBuffer();
  delay(500);
  u8g2.drawStr(0,39,"Syncing Time");
  u8g2.sendBuffer();
  delay(500);
  u8g2.drawStr(0,52,"Syncing Weather");
  u8g2.sendBuffer();
  delay(500);
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_luBIS14_te);
  u8g2.drawStr(0,25,"System Init");
  u8g2.drawStr(19,53,"Success");
  u8g2.sendBuffer();
  delay(800);
  if(bNeedinit)
  {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_wqy14_t_gb2312a);
    u8g2.drawUTF8(0,20,"请链接热点来配置AP:");
    u8g2.drawUTF8(0,45,("ESP8266_"+(String)ESP.getChipId()).c_str());
    u8g2.sendBuffer();
  }
}

void loop() {
  udpServer();
  server.handleClient();
  if (!bNeedinit)
  {
    bool isgetweather;
    if (localTime.tm_year < (2016 - 1900) && !bNeedinit)
    {
      if (millis() - otime > 5000)
      {
        otime = millis();
        getNtpTime();
      }
    }
    while (bNeedUpdate)
    {
      httpOTA(udp_server.remoteIP());
    }
    if (millis() - dtime > 1000)
    {
      dtime = millis();
      getLocalTime();
    }
      //drawWatch();
    if (millis() - weather.time > 60000)
    {
      isgetweather =  getWeather();
    }
    if(isgetweather)
    {
      drawWeather();
    }else
    {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_wqy14_t_gb2312a);
    u8g2.drawUTF8(0,14,"获取天气失败");
    u8g2.drawUTF8(0,30,"请检查网络连接");
    u8g2.drawUTF8(0,46,"APPID APPSERCET");
    u8g2.sendBuffer();
    }
  }
  if (bNeedinit)
    dnsServer.processNextRequest();
  delay(1);
}