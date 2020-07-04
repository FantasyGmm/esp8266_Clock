#include <Arduino.h>
#include<U8g2lib.h>
#include<ESP8266WiFi.h>
#include<WiFiClient.h>
#include<ESP8266WebServer.h>
#include<html.h>
#include <Arduino_JSON.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <FS.h>
#include <LittleFS.h>
#include <time.h>
#include <DNSServer.h>

/*
//I2C
     OLED  ---  ESP8266
     VCC   ---  3.3V / 5V
     GND   ---  G (GND)
     SCL   ---  D1(GPIO5)
     SDA   ---  D2(GPIO4)
*/
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/U8X8_PIN_NONE);
bool needupdate;
void httpServer()
{
  ESP8266WebServer server(80);
}
void handleRoot(){

}
void  handleNotFound(){

}
void drawStr(u8g2_uint_t x,u8g2_uint_t y,const char *text)
{
  u8g2.drawStr(x,y,text);
}
void  drawXBM(uint8_t width, uint8_t height, uint8_t *bmp)
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
};
void initdAP()
{
  IPAddress  APip(192,168,1,1);
  IPAddress Subnet(255,255,255,0);
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(APip,APip,Subnet);
  String APname = ("ESP8266_"+(String)ESP.getChipId());
  WiFi.softAP(APname.c_str());
  u8g2.firstPage();
  do{
  u8g2.setFont(u8g2_font_wqy12_t_gb2312a);
    drawStr(0,20,APname.c_str());
  }while (u8g2.nextPage());
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
void setup() {
  Serial.begin(115200);
  u8g2.begin();
  u8g2.initDisplay();
  u8g2.clearBuffer();
  u8g2.enableUTF8Print();
  initdAP();
}

void loop() {
  // put your main code here, to run repeatedly:
  while (needupdate)
  {
    //httpOTA();
  }
}