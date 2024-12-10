/* *****************************************************************
 *
 * SmallDesktopDisplay
 *    小型桌面显示器
 *
 * 原  作  者：Misaka
 * 修      改：微车游
 * 讨  论  群：811058758、887171863
 * 创 建 日 期：2021.07.19
 * 最后更改日期：2021.09.18
 * 更 改 说 明：V1.1添加串口调试，波特率115200\8\n\1；增加版本号显示。
 *            V1.2亮度和城市代码保存到EEPROM，断电可保存
 *            V1.3.1
 * 更改smartconfig改为WEB配网模式，同时在配网的同时增加亮度、屏幕方向设置。
 *            V1.3.2
 * 增加wifi休眠模式，仅在需要连接的情况下开启wifi，其他时间关闭wifi。增加wifi保存至eeprom（目前仅保存一组ssid和密码）
 *            V1.3.3  修改WiFi保存后无法删除的问题。目前更改为使用串口控制，输入
 * 0x05 重置WiFi数据并重启。 增加web配网以及串口设置天气更新时间的功能。 V1.3.4
 * 修改web配网页面设置，将wifi设置页面以及其余设置选项放入同一页面中。
 *                    增加web页面设置是否使用DHT传感器。（使能DHT后才可使用）
 *            V1.4
 * 增加web服务器，使用web网页进行设置。由于使用了web服务器，无法开启WiFi休眠。
 *                    注意，此版本中的DHT11传感器和太空人图片选择可以通过web网页设置来进行选择，无需通过使能标志来重新编译。
 *
 * 引 脚 分 配： SCK  GPIO14
 *             MOSI  GPIO13
 *             RES   GPIO2
 *             DC    GPIO0
 *             LCDBL GPIO5
 *
 *             增加DHT11温湿度传感器，传感器接口为 GPIO 12
 *
 *    感谢群友 @你别失望
 * 提醒发现WiFi保存后无法重置的问题，目前已解决。详情查看更改说明！
 * *****************************************************************/
#define Version "SDD V1.4"
/* *****************************************************************
 *  库文件、头文件
 * *****************************************************************/
#include "ArduinoJson.h"
#include "FS.h"
#include "number.h"
#include "qr.h"
#include "weathernum.h"
#include <EEPROM.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>
#include <Adafruit_NeoPixel.h>

// Web服务器使能标志位----打开后将无法使用wifi休眠功能。
#define WebSever_EN 1

// 设置太空人图片是否使用
#define imgAst_EN 1

#define PIN_LED 48
#define PIN_I2S_SD 4
#define PIN_I2S_DOUT 5
#define PIN_I2S_BCLK 6
#define PIN_I2S_LRC 7
#define PIN_PLAY_BTN 9

#include <WiFiManager.h>

WiFiManager wm;

/* *****************************************************************
 *  字库、图片库
 * *****************************************************************/
#include "font/ZoloFont_20.h"
#include "img/humidity.h"
#include "img/misaka.h"
#include "img/temperature.h"

#if imgAst_EN

#include "img/pangzi/i0.h"
#include "img/pangzi/i1.h"
#include "img/pangzi/i2.h"
#include "img/pangzi/i3.h"
#include "img/pangzi/i4.h"
#include "img/pangzi/i5.h"
#include "img/pangzi/i6.h"
#include "img/pangzi/i7.h"
#include "img/pangzi/i8.h"
#include "img/pangzi/i9.h"

int Anim = 0;      // 太空人图标显示指针记录
int AprevTime = 0; // 太空人更新时间记录
#endif

/* *****************************************************************
 *  参数设置
 * *****************************************************************/

struct config_type {
  char stassid[32]; // 定义配网得到的WIFI名长度(最大32字节)
  char stapsw[64];  // 定义配网得到的WIFI密码长度(最大64字节)
};

//---------------修改此处""内的信息--------------------
// 如开启WEB配网则可不用设置这里的参数，前一个为wifi ssid，后一个为密码
config_type wificonf = {{""}, {""}};

// 天气更新时间  X 分钟
int updateweater_time = 10;
// LCD屏幕方向
int LCD_Rotation = 3;
// 屏幕亮度0-100，默认50
int LCD_BL_PWM = 50;
// 天气城市代码 长沙:101250101株洲:101250301衡阳:101250401
String cityCode = "101280601";
//----------------------------------------------------

// LCD屏幕相关设置
//  引脚请自行配置tft_espi库中的 User_Setup.h文件
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite clk = TFT_eSprite(&tft);
// LCD背光引脚
#define LCD_BL_PIN TFT_BL
uint16_t bgColor = 0x0000;

// 其余状态标志位
uint8_t Wifi_en = 1;         // wifi状态标志位  1：打开    0：关闭
uint8_t UpdateWeater_en = 0; // 更新时间标志位
int prevTime = 0;            // 滚动显示更新标志位
int DHT_img_flag = 0;        // DHT传感器使用标志位

// EEPROM参数存储地址位
int BL_addr = 1;    // 被写入数据的EEPROM地址编号  1亮度
int Ro_addr = 2;    // 被写入数据的EEPROM地址编号  2 旋转方向
int DHT_addr = 3;   // 3 DHT使能标志位
int UpWeT_addr = 4; // 4 更新时间记录
int CC_addr = 10;   // 被写入数据的EEPROM地址编号  10城市
int wifi_addr = 30; // 被写入数据的EEPROM地址编号  20wifi-ssid-psw

time_t prevDisplay = 0;       // 显示时间显示记录
unsigned long weaterTime = 0; // 天气更新时间记录
String SMOD = "";             // 串口数据存储

/*** Component objects ***/
Number dig;
WeatherNum wrat;

uint32_t targetTime = 0;

int tempnum = 0;      // 温度百分比
int huminum = 0;      // 湿度百分比
int tempcol = 0xffff; // 温度显示颜色
int humicol = 0xffff; // 湿度显示颜色

// Web网站服务器
//  建立esp8266网站服务器对象
WebServer server(80);

WiFiClient wificlient;
float duty = 0;

Adafruit_NeoPixel pixels(4, PIN_LED, NEO_GRB + NEO_KHZ800);

void digitalClockDisplay(int reflash_en);

void printDigits(int digits);

String num2str(int digits);

void sendNTPpacket(IPAddress &address);

void LCD_reflash(int en);

void savewificonfig();

void readwificonfig();

void deletewificonfig();

#if WebSever_EN

void Web_Sever_Init();

void Web_Sever();

#endif

void saveCityCodetoEEP(int *citycode);

void readCityCodefromEEP(int *citycode);

void getCityCode();

void getCityWeater();

void saveParamCallback();

void scrollBanner();

void imgAnim();

void weaterData(String *cityDZ, String *dataSK, String *dataFC);

void inline startConfigTime() {
  const int timeZone = 8 * 3600;
  configTime(timeZone, 0, "ntp6.aliyun.com", "cn.ntp.org.cn", "ntp.ntsc.ac.cn");
  while (time(nullptr) < 8 * 3600 * 2) {
    delay(500);
  }
}

/* *****************************************************************
 *  函数
 * *****************************************************************/
// 读取保存城市代码
void saveCityCodetoEEP(int *citycode) {
  for (int cnum = 0; cnum < 5; cnum++) {
    // 城市地址写入城市代码
    EEPROM.write(CC_addr + cnum, *citycode % 100);
    EEPROM.commit(); // 保存更改的数据
    *citycode = *citycode / 100;
    delay(5);
  }
}

void readCityCodefromEEP(int *citycode) {
  for (int cnum = 5; cnum > 0; cnum--) {
    *citycode = *citycode * 100;
    *citycode += EEPROM.read(CC_addr + cnum - 1);
    delay(5);
  }
}

// wifi ssid，psw保存到eeprom
void savewificonfig() {
  // 开始写入
  uint8_t *p = (uint8_t *)(&wificonf);
  for (int i = 0; i < sizeof(wificonf); i++) {
    EEPROM.write(i + wifi_addr, *(p + i)); // 在闪存内模拟写入
  }
  delay(10);
  EEPROM.commit(); // 执行写入ROM
  delay(10);
}

// 删除原有eeprom中的信息
void deletewificonfig() {
  config_type deletewifi = {{""}, {""}};
  uint8_t *p = (uint8_t *)(&deletewifi);
  for (int i = 0; i < sizeof(deletewifi); i++) {
    EEPROM.write(i + wifi_addr, *(p + i)); // 在闪存内模拟写入
  }
  delay(10);
  EEPROM.commit(); // 执行写入ROM
  delay(10);
}

// 从eeprom读取WiFi信息ssid，psw
void readwificonfig() {
  uint8_t *p = (uint8_t *)(&wificonf);
  for (int i = 0; i < sizeof(wificonf); i++) {
    *(p + i) = EEPROM.read(i + wifi_addr);
  }
  Serial.printf("Read WiFi Config.....\r\n");
  Serial.printf("SSID:%s\r\n", wificonf.stassid);
  Serial.printf("PSW:%s\r\n", wificonf.stapsw);
  Serial.printf("Connecting.....\r\n");
}

// TFT屏幕输出函数
bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h,
                uint16_t *bitmap) {
  if (y >= tft.height())
    return 0;
  tft.pushImage(x, y, w, h, bitmap);
  // Return 1 to decode next block
  return 1;
}

// 进度条函数
byte loadNum = 6;

// 绘制进度条
void loading(byte delayTime) {
  clk.setColorDepth(8);

  clk.createSprite(200, 100); // 创建窗口
  clk.fillSprite(0x0000);     // 填充率

  clk.drawRoundRect(0, 0, 200, 16, 4, 0xFFFF);        // 空心圆角矩形
  clk.fillRoundRect(3, 3, loadNum, 10, 2, TFT_GREEN); // 实心圆角矩形
  clk.setTextDatum(CC_DATUM);                         // 设置文本数据
  clk.setTextColor(TFT_GREEN, 0x0000);
  clk.drawString("Connecting to WiFi......", 100, 40, 2);
  clk.setTextColor(TFT_WHITE, 0x0000);
  clk.drawRightString(Version, 180, 60, 2);
  clk.pushSprite(20, 120); // 窗口位置

  clk.deleteSprite();
  loadNum += 1;
  delay(delayTime);
}

// 湿度图标显示函数
void humidityWin() {
  clk.setColorDepth(8);
  huminum = huminum / 2;
  clk.createSprite(52, 6); // 创建窗口
  clk.fillSprite(0x0000);  // 填充率
  // 空心圆角矩形  起始位x,y,长度，宽度，圆弧半径，颜色
  clk.drawRoundRect(0, 0, 52, 6, 3, 0xFFFF);
  clk.fillRoundRect(1, 1, huminum, 4, 2, humicol); // 实心圆角矩形
  clk.pushSprite(45, 222);                         // 窗口位置
  clk.deleteSprite();
}

// 温度图标显示函数
void tempWin() {
  clk.setColorDepth(8);
  clk.createSprite(52, 6); // 创建窗口
  clk.fillSprite(0x0000);  // 填充率
  // 空心圆角矩形  起始位x,y,长度，宽度，圆弧半径，颜色
  clk.drawRoundRect(0, 0, 52, 6, 3, 0xFFFF);
  clk.fillRoundRect(1, 1, tempnum, 4, 2, tempcol); // 实心圆角矩形
  clk.pushSprite(45, 192);                         // 窗口位置
  clk.deleteSprite();
}

// 串口调试设置函数
void Serial_set() {
  String incomingByte = "";
  if (Serial.available() > 0) {
    // 监测串口缓存，当有数据输入时，循环赋值给incomingByte
    while (Serial.available() > 0) {
      // 读取单个字符值，转换为字符，并按顺序一个个赋值给incomingByte
      incomingByte += char(Serial.read());
      // 不能省略，因为读取缓冲区数据需要时间
      delay(2);
    }
    // 设置1亮度设置
    if (SMOD == "0x01") {
      // int n = atoi(xxx.c_str());//String转int
      int LCDBL = atoi(incomingByte.c_str());
      if (LCDBL >= 0 && LCDBL <= 100) {
        // 亮度地址写入亮度值
        EEPROM.write(BL_addr, LCDBL);
        EEPROM.commit(); // 保存更改的数据
        delay(5);
        LCD_BL_PWM = EEPROM.read(BL_addr);
        delay(5);
        SMOD = "";
        Serial.printf("亮度调整为：");
        analogWrite(LCD_BL_PIN, 1023 - (LCD_BL_PWM * 10));
        Serial.println(LCD_BL_PWM);
        Serial.println("");
      } else {
        Serial.println("亮度调整错误，请输入0-100");
      }
    }
    // 设置2地址设置
    if (SMOD == "0x02") {
      int CityCODE = 0;
      // int n = atoi(xxx.c_str());//String转int
      int CityC = atoi(incomingByte.c_str());
      if (CityC >= 101000000 && CityC <= 102000000 || CityC == 0) {
        saveCityCodetoEEP(&CityC);
        readCityCodefromEEP(&CityC);
        cityCode = CityCODE;
        if (cityCode == "0") {
          Serial.println("城市代码调整为：自动");
          getCityCode(); // 获取城市代码
        } else {
          Serial.printf("城市代码调整为：");
          Serial.println(cityCode);
        }
        Serial.println("");
        getCityWeater(); // 更新城市天气
        SMOD = "";
      } else {
        Serial.println("城市调整错误，请输入9位城市代码，自动获取请输入0");
      }
    }
    // 设置3屏幕显示方向
    if (SMOD == "0x03") {
      int RoSet = atoi(incomingByte.c_str());
      if (RoSet >= 0 && RoSet <= 3) {
        // 屏幕方向地址写入方向值
        EEPROM.write(Ro_addr, RoSet);
        EEPROM.commit(); // 保存更改的数据
        SMOD = "";
        // 设置屏幕方向后重新刷屏并显示
        tft.setRotation(RoSet);
        tft.fillScreen(0x0000);
        // 屏幕刷新程序
        LCD_reflash(1);
        UpdateWeater_en = 1;
        // 温度图标
        TJpgDec.drawJpg(15, 183, temperature, sizeof(temperature));
        // 湿度图标
        TJpgDec.drawJpg(15, 213, humidity, sizeof(humidity));
        Serial.print("屏幕方向设置为：");
        Serial.println(RoSet);
      } else {
        Serial.println("屏幕方向值错误，请输入0-3内的值");
      }
    }
    // 设置天气更新时间
    if (SMOD == "0x04") {
      // int n = atoi(xxx.c_str());//String转int
      int wtup = atoi(incomingByte.c_str());
      if (wtup >= 1 && wtup <= 60) {
        // 亮度地址写入亮度值
        EEPROM.write(UpWeT_addr, wtup);
        // 保存更改的数据
        EEPROM.commit();
        delay(5);
        updateweater_time = wtup;
        SMOD = "";
        Serial.printf("天气更新时间更改为：");
        Serial.print(updateweater_time);
        Serial.println("分钟");
      } else {
        Serial.println("更新时间太长，请重新设置（1-60）");
      }
    } else {
      SMOD = incomingByte;
      delay(2);
      if (SMOD == "0x01")
        Serial.println("请输入亮度值，范围0-100");
      else if (SMOD == "0x02")
        Serial.println("请输入9位城市代码，自动获取请输入0");
      else if (SMOD == "0x03") {
        Serial.println("请输入屏幕方向值，");
        Serial.println("0-USB接口朝下");
        Serial.println("1-USB接口朝右");
        Serial.println("2-USB接口朝上");
        Serial.println("3-USB接口朝左");
      } else if (SMOD == "0x04") {
        Serial.print("当前天气更新时间：");
        Serial.print(updateweater_time);
        Serial.println("分钟");
        Serial.println("请输入天气更新时间（1-60）分钟");
      } else if (SMOD == "0x05") {
        Serial.println("重置WiFi设置中......");
        delay(10);
        wm.resetSettings();
        deletewificonfig();
        delay(10);
        Serial.println("重置WiFi成功");
        SMOD = "";
        ESP.restart();
      } else {
        Serial.println("");
        Serial.println("请输入需要修改的代码：");
        Serial.println("亮度设置输入        0x01");
        Serial.println("地址设置输入        0x02");
        Serial.println("屏幕方向设置输入    0x03");
        Serial.println("更改天气更新时间    0x04");
        Serial.println("重置WiFi(会重启)    0x05");
        Serial.println("");
      }
    }
  }
}

#if WebSever_EN

// web网站相关函数
// web设置页面
void handleconfig() {
  String msg;
  int web_cc, web_setro, web_lcdbl, web_upt, web_dhten;

  if (server.hasArg("web_ccode") || server.hasArg("web_bl") ||
      server.hasArg("web_upwe_t") || server.hasArg("web_DHT11_en") ||
      server.hasArg("web_set_rotation")) {
    web_cc = server.arg("web_ccode").toInt();
    web_setro = server.arg("web_set_rotation").toInt();
    web_lcdbl = server.arg("web_bl").toInt();
    web_upt = server.arg("web_upwe_t").toInt();
    web_dhten = server.arg("web_DHT11_en").toInt();
    Serial.println("");
    if (web_cc >= 101000000 && web_cc <= 102000000) {
      saveCityCodetoEEP(&web_cc);
      readCityCodefromEEP(&web_cc);
      cityCode = web_cc;
      Serial.print("城市代码:");
      Serial.println(web_cc);
    }
    if (web_lcdbl > 0 && web_lcdbl <= 100) {
      EEPROM.write(BL_addr, web_lcdbl); // 亮度地址写入亮度值
      EEPROM.commit();                  // 保存更改的数据
      delay(5);
      LCD_BL_PWM = EEPROM.read(BL_addr);
      delay(5);
      Serial.printf("亮度调整为：");
      analogWrite(LCD_BL_PIN, 1023 - (LCD_BL_PWM * 10));
      Serial.println(LCD_BL_PWM);
      Serial.println("");
    }
    if (web_upt > 0 && web_upt <= 60) {
      EEPROM.write(UpWeT_addr, web_upt); // 亮度地址写入亮度值
      EEPROM.commit();                   // 保存更改的数据
      delay(5);
      updateweater_time = web_upt;
      Serial.print("天气更新时间（分钟）:");
      Serial.println(web_upt);
    }
    EEPROM.write(DHT_addr, web_dhten);
    EEPROM.commit(); // 保存更改的数据
    delay(5);
    if (web_dhten != DHT_img_flag) {
      DHT_img_flag = web_dhten;
      tft.fillScreen(0x0000);
      LCD_reflash(1); // 屏幕刷新程序
      UpdateWeater_en = 1;
      TJpgDec.drawJpg(15, 183, temperature, sizeof(temperature)); // 温度图标
      TJpgDec.drawJpg(15, 213, humidity, sizeof(humidity)); // 湿度图标
    }
    Serial.print("DHT Sensor Enable： ");
    Serial.println(DHT_img_flag);
    EEPROM.write(Ro_addr, web_setro);
    EEPROM.commit(); // 保存更改的数据
    delay(5);
    if (web_setro != LCD_Rotation) {
      LCD_Rotation = web_setro;
      tft.setRotation(LCD_Rotation);
      tft.fillScreen(0x0000);
      LCD_reflash(1); // 屏幕刷新程序
      UpdateWeater_en = 1;
      TJpgDec.drawJpg(15, 183, temperature, sizeof(temperature)); // 温度图标
      TJpgDec.drawJpg(15, 213, humidity, sizeof(humidity)); // 湿度图标
    }
    Serial.print("LCD Rotation:");
    Serial.println(LCD_Rotation);
  }

  // 网页界面代码段
  String content = "<html><meta charset='UTF-8'><style>html,body{ background: "
                   "#1aceff; color: #fff; font-size: 10px;}</style>";
  content += "<body><form action='/' method='POST'><br><div>天气时钟</div><br>";
  content += "城市代码:<br><input type='text' name='web_ccode' "
             "placeholder='city code'><br>";
  content += "<br>时钟亮度(1-100):(推荐:15)<br><input type='text' "
             "name='web_bl' placeholder='10'><br>";
  content += "<br>天气更新分钟:(默认:10分钟)<br><input type='text' "
             "name='web_upwe_t' placeholder='10'><br>";
  content += "<br>屏幕方向<br>\
                    <input type='radio' name='web_set_rotation' value='0' checked> USB 向下<br>\
                    <input type='radio' name='web_set_rotation' value='1'> USB 向右<br>\
                    <input type='radio' name='web_set_rotation' value='2'> USB 向上<br>\
                    <input type='radio' name='web_set_rotation' value='3'> USB 向左<br>";
  content +=
      "<br><div><input type='submit' name='Save' value='Save'></form></div>" +
      msg + "<br>";
  content += "</body></html>";
  server.send(200, "text/html", content);
}

// no need authentication
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

// Web服务初始化
void Web_Sever_Init() {
  uint32_t counttime = 0; // 记录创建mDNS的时间
  Serial.println("mDNS responder building...");
  counttime = millis();
  while (!MDNS.begin("SD3")) {
    // 判断超过30秒钟就重启设备
    if (millis() - counttime > 30000)
      ESP.restart();
  }
  Serial.println("mDNS responder started");

  server.on("/", handleconfig);
  server.onNotFound(handleNotFound);

  // 开启TCP服务
  server.begin();
  Serial.println("HTTP服务器已开启");

  Serial.println("连接: http://sd3.local");
  Serial.print("本地IP： ");
  Serial.println(WiFi.localIP());
  // 将服务器添加到mDNS
  MDNS.addService("http", "tcp", 80);
}

// Web网页设置函数
void Web_Sever() {
  //MDNS.update();
  server.handleClient();
}

// web服务打开后LCD显示登陆网址及IP
void Web_sever_Win() {
  // IPAddress IP_adr = WiFi.localIP();
  clk.setColorDepth(8);

  clk.createSprite(200, 70); // 创建窗口
  clk.fillSprite(0x0000);    // 填充率

  clk.setTextDatum(CC_DATUM); // 设置文本数据
  clk.setTextColor(TFT_GREEN, 0x0000);
  clk.drawString("Connect to Config:", 70, 10, 2);
  clk.setTextColor(TFT_WHITE, 0x0000);
  clk.drawString("http://sd3.local", 100, 40, 4);
  clk.pushSprite(20, 40); // 窗口位置

  clk.deleteSprite();
}

#endif

// WEB配网LCD显示函数
void Web_win() {
  clk.setColorDepth(8);

  clk.createSprite(200, 60); // 创建窗口
  clk.fillSprite(0x0000);    // 填充率

  clk.setTextDatum(CC_DATUM); // 设置文本数据
  clk.setTextColor(TFT_GREEN, 0x0000);
  clk.drawString("WiFi Connect Fail!", 100, 10, 2);
  clk.drawString("SSID:", 45, 40, 2);
  clk.setTextColor(TFT_WHITE, 0x0000);
  clk.drawString("AutoConnectAP", 125, 40, 2);
  clk.pushSprite(20, 50); // 窗口位置

  clk.deleteSprite();
}

// WEB配网函数
void Webconfig() {
  WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP
  delay(3000);
  wm.resetSettings(); // wipe settings

  // add a custom input field
  // int customFieldLength = 40;
  const char *set_rotation =
      "<br/><label for='set_rotation'>Set Rotation</label>\
                              <input type='radio' name='set_rotation' value='0' checked> One<br>\
                              <input type='radio' name='set_rotation' value='1'> Two<br>\
                              <input type='radio' name='set_rotation' value='2'> Three<br>\
                              <input type='radio' name='set_rotation' value='3'> Four<br>";
  WiFiManagerParameter custom_rot(set_rotation); // custom html input
  WiFiManagerParameter custom_bl("LCDBL", "LCD BackLight(1-100)", "10", 3);
  WiFiManagerParameter custom_weatertime("WeaterUpdateTime",
                                         "Weather Update Time(Min)", "10", 3);
  WiFiManagerParameter custom_cc("CityCode", "CityCode", cityCode.c_str(), 9);
  WiFiManagerParameter p_lineBreak_notext("<p></p>");

  wm.addParameter(&p_lineBreak_notext);
  wm.addParameter(&custom_cc);
  wm.addParameter(&p_lineBreak_notext);
  wm.addParameter(&custom_bl);
  wm.addParameter(&p_lineBreak_notext);
  wm.addParameter(&custom_weatertime);
  wm.addParameter(&p_lineBreak_notext);
  wm.addParameter(&custom_rot);
  wm.setSaveParamsCallback(saveParamCallback);
  std::vector<const char *> menu = {"wifi", "restart"};
  wm.setMenu(menu);

  // set dark theme
  wm.setClass("invert");
  // set min RSSI (percentage) to show in scans, null = 8%
  wm.setMinimumSignalQuality(20); 
  bool res;
  // anonymous ap
  res = wm.autoConnect("AutoConnectAP"); 
  while (!res){
  }
}

String getParam(String name) {
  // read parameter from server, for customhmtl input
  String value;
  if (wm.server->hasArg(name)) {
    value = wm.server->arg(name);
  }
  return value;
}

void saveParamCallback() {
  int cc;
  Serial.println("[CALLBACK] saveParamCallback fired");
  updateweater_time = getParam("WeaterUpdateTime").toInt();
  cc = getParam("CityCode").toInt();
  LCD_Rotation = getParam("set_rotation").toInt();
  LCD_BL_PWM = getParam("LCDBL").toInt();
  // 对获取的数据进行处理
  // 城市代码
  Serial.print("CityCode = ");
  Serial.println(cc);
  if (cc >= 101000000 && cc <= 102000000 || cc == 0) {
    saveCityCodetoEEP(&cc);
    readCityCodefromEEP(&cc);
    cityCode = cc;
  }
  // 屏幕方向
  Serial.print("LCD_Rotation = ");
  Serial.println(LCD_Rotation);
  if (EEPROM.read(Ro_addr) != LCD_Rotation) {
    EEPROM.write(Ro_addr, LCD_Rotation);
    EEPROM.commit();
    delay(5);
  }
  tft.setRotation(LCD_Rotation);
  tft.fillScreen(0x0000);
  Web_win();
  loadNum--;
  loading(1);
  if (EEPROM.read(BL_addr) != LCD_BL_PWM) {
    EEPROM.write(BL_addr, LCD_BL_PWM);
    EEPROM.commit();
    delay(5);
  }
  // 屏幕亮度
  Serial.printf("亮度调整为：");
  analogWrite(LCD_BL_PIN, 1023 - (LCD_BL_PWM * 10));
  Serial.println(LCD_BL_PWM);
  // 天气更新时间
  Serial.printf("天气更新时间调整为：");
  Serial.println(updateweater_time);
}

void inline initPixels() {
  pixels.begin();
  pixels.setBrightness(100);
  pixels.clear();
  pixels.show();
}

void inline pixelsRandomShow(){
  uint16_t rc = rand() % 65536;
  pixels.rainbow(rc);
  pixels.show();
}

void setup() {
  Serial.begin(115200);
  EEPROM.begin(1024);
  // 在初始化中使wifi重置，需重新配置WiFi
  wm.resetSettings();

  initPixels();

  // 从eeprom读取背光亮度设置
  auto bl = EEPROM.read(BL_addr);
  if (bl > 0 && bl < 100) {
    LCD_BL_PWM = bl;
  }
  pinMode(LCD_BL_PIN, OUTPUT);
  analogWriteResolution(10);
  analogWriteFrequency(25000);
  analogWrite(LCD_BL_PIN, 1023 - (LCD_BL_PWM * 10));
  // 从eeprom读取屏幕方向设置
  auto rot = EEPROM.read(Ro_addr);
  if (rot >= 0 && rot <= 3) {
    LCD_Rotation = rot;
  }
  // 从eeprom读取天气更新时间
  updateweater_time = EEPROM.read(UpWeT_addr);

  tft.begin();          /* TFT init */
  tft.invertDisplay(1); // 反转所有显示颜色：1反转，0正常
  tft.setRotation(LCD_Rotation);
  tft.fillScreen(0x0000);
  tft.setTextColor(TFT_BLACK, bgColor);

  targetTime = millis() + 1000;
  readwificonfig(); // 读取存储的wifi信息
  Serial.print("正在连接WIFI ");
  Serial.println(wificonf.stassid);
  WiFi.begin(wificonf.stassid, wificonf.stapsw);
  TJpgDec.setJpgScale(1);
  TJpgDec.setSwapBytes(true);
  TJpgDec.setCallback(tft_output);

  while (WiFi.status() != WL_CONNECTED) {
    loading(30);
    if (loadNum >= 194) {
      Web_win();
      Webconfig();
      break;
    }
  }
  delay(10);
  // 让动画走完
  while (loadNum < 194) {
    loading(1);
  }
  if (WiFi.status() == WL_CONNECTED) {
    strcpy(wificonf.stassid, WiFi.SSID().c_str()); // 名称复制
    strcpy(wificonf.stapsw, WiFi.psk().c_str());   // 密码复制
    savewificonfig();
    readwificonfig();
#if WebSever_EN
    // 开启web服务器初始化
    Web_Sever_Init();
    Web_sever_Win();
    startConfigTime();
    delay(5000);
#endif
  }

  TJpgDec.setJpgScale(1);
  TJpgDec.setSwapBytes(true);
  TJpgDec.setCallback(tft_output);

  int CityCODE = 0;
  readCityCodefromEEP(&CityCODE);
  if (CityCODE >= 101000000 && CityCODE <= 102000000) {
    cityCode = CityCODE;
  } else {
    getCityCode(); // 获取城市代码
  }
  tft.fillScreen(TFT_BLACK);                                  // 清屏
  TJpgDec.drawJpg(15, 183, temperature, sizeof(temperature)); // 温度图标
  TJpgDec.drawJpg(15, 213, humidity, sizeof(humidity));       // 湿度图标
  getCityWeater();

#if !WebSever_EN
  WiFi.forceSleepBegin(); // wifi off
  Serial.println("WIFI休眠......");
  Wifi_en = 0;
#endif
}

void loop() {
#if WebSever_EN
  Web_Sever();
#endif
  LCD_reflash(0);
  Serial_set();
}

void LCD_reflash(int en) {
  struct tm info;
  getLocalTime(&info);

  if (info.tm_sec != prevDisplay || en == 1) {
    prevDisplay = info.tm_sec;
    digitalClockDisplay(en);
    prevTime = 0;
  }
  // 两秒钟更新一次
  if (info.tm_sec % 2 == 0 && prevTime == 0 || en == 1) {
    scrollBanner();
    pixelsRandomShow();
  }
#if imgAst_EN
  if (DHT_img_flag == 0)
    imgAnim();
#endif
  // 10分钟更新一次天气
  if (millis() - weaterTime > (60000 * updateweater_time) || en == 1 ||
      UpdateWeater_en != 0) {
    if (Wifi_en == 0) {
      //WiFi.forceSleepWake(); // wifi on
      Serial.println("WIFI恢复......");
      Wifi_en = 1;
    }
    if (WiFi.status() == WL_CONNECTED) {
      getCityWeater();
      if (UpdateWeater_en != 0)
        UpdateWeater_en = 0;
      weaterTime = millis();
#if !WebSever_EN
      WiFi.forceSleepBegin(); // Wifi Off
      Serial.println("WIFI休眠......");
      Wifi_en = 0;
#endif
    }
  }
}

// 发送HTTP请求并且将服务器响应通过串口输出
void getCityCode() {
  String URL = "http://wgeo.weather.com.cn/ip/?_=" + String(millis());
  // 创建 HTTPClient 对象
  HTTPClient httpClient;
  // 配置请求地址。此处也可以不使用端口号和PATH而单纯的
  httpClient.begin(wificlient, URL);
  // 设置请求头中的User-Agent
  httpClient.setUserAgent("Mozilla/5.0 (iPhone; CPU iPhone OS 11_0 like Mac OS "
                          "X) AppleWebKit/604.1.38 (KHTML, like Gecko) "
                          "Version/11.0 Mobile/15A372 Safari/604.1");
  httpClient.addHeader("Referer", "http://www.weather.com.cn/");

  // 启动连接并发送HTTP请求
  int httpCode = httpClient.GET();
  Serial.print("Send GET request to URL: ");
  Serial.println(URL);

  // 如果服务器响应OK则从服务器获取响应体信息并通过串口输出
  if (httpCode == HTTP_CODE_OK) {
    String str = httpClient.getString();
    int aa = str.indexOf("id=");
    if (aa > -1) {
      // cityCode = str.substring(aa+4,aa+4+9).toInt();
      cityCode = str.substring(aa + 4, aa + 4 + 9);
      Serial.println(cityCode);
      getCityWeater();
    } else {
      Serial.println("获取城市代码失败");
    }
  } else {
    Serial.println("请求城市代码错误：");
    Serial.println(httpCode);
  }
  // 关闭ESP8266与服务器连接
  httpClient.end();
}

// 获取城市天气
void getCityWeater() {
  // String URL = "http://d1.weather.com.cn/dingzhi/" + cityCode +
  // ".html?_="+String(now());//新
  String URL = "http://d1.weather.com.cn/weather_index/" + cityCode +
               ".html?_=" + String(millis()); // 原来
  // 创建 HTTPClient 对象
  HTTPClient httpClient;
  httpClient.begin(wificlient, URL);

  // 设置请求头中的User-Agent
  httpClient.setUserAgent("Mozilla/5.0 (iPhone; CPU iPhone OS 11_0 like Mac OS "
                          "X) AppleWebKit/604.1.38 (KHTML, like Gecko) "
                          "Version/11.0 Mobile/15A372 Safari/604.1");
  httpClient.addHeader("Referer", "http://www.weather.com.cn/");

  // 启动连接并发送HTTP请求
  int httpCode = httpClient.GET();
  Serial.println("正在获取天气数据");
  Serial.println(URL);

  // 如果服务器响应OK则从服务器获取响应体信息并通过串口输出
  if (httpCode == HTTP_CODE_OK) {

    String str = httpClient.getString();
    int indexStart = str.indexOf("weatherinfo\":");
    int indexEnd = str.indexOf("};var alarmDZ");

    String jsonCityDZ = str.substring(indexStart + 13, indexEnd);

    indexStart = str.indexOf("dataSK =");
    indexEnd = str.indexOf(";var dataZS");
    String jsonDataSK = str.substring(indexStart + 8, indexEnd);

    indexStart = str.indexOf("\"f\":[");
    indexEnd = str.indexOf(",{\"fa");
    String jsonFC = str.substring(indexStart + 5, indexEnd);

    weaterData(&jsonCityDZ, &jsonDataSK, &jsonFC);
    Serial.println("获取成功");
  } else {
    Serial.println("请求城市天气错误：");
    Serial.print(httpCode);
  }
  // 关闭ESP8266与服务器连接
  httpClient.end();
}

String scrollText[7]; // 天气信息存储

// 天气信息写到屏幕上
void weaterData(String *cityDZ, String *dataSK, String *dataFC) {
  // 解析第一段JSON
  JsonDocument doc;
  deserializeJson(doc, *dataSK);
  JsonObject sk = doc.as<JsonObject>();

  /***绘制相关文字***/
  clk.setColorDepth(8);
  clk.loadFont(ZoloFont_20);

  // 温度
  clk.createSprite(58, 24);
  clk.fillSprite(bgColor);
  clk.setTextDatum(CC_DATUM);
  clk.setTextColor(TFT_WHITE, bgColor);
  clk.drawString(sk["temp"].as<String>() + "℃", 28, 13);
  clk.pushSprite(100, 184);
  clk.deleteSprite();
  tempnum = sk["temp"].as<int>();
  tempnum = tempnum + 10;
  if (tempnum < 10)
    tempcol = 0x00FF;
  else if (tempnum < 28)
    tempcol = 0x0AFF;
  else if (tempnum < 34)
    tempcol = 0x0F0F;
  else if (tempnum < 41)
    tempcol = 0xFF0F;
  else if (tempnum < 49)
    tempcol = 0xF00F;
  else {
    tempcol = 0xF00F;
    tempnum = 50;
  }
  tempWin();

  // 湿度
  clk.createSprite(58, 24);
  clk.fillSprite(bgColor);
  clk.setTextDatum(CC_DATUM);
  clk.setTextColor(TFT_WHITE, bgColor);
  clk.drawString(sk["SD"].as<String>(), 28, 13);
  clk.pushSprite(100, 214);
  clk.deleteSprite();
  huminum = atoi((sk["SD"].as<String>()).substring(0, 2).c_str());

  if (huminum > 90)
    humicol = 0x00FF;
  else if (huminum > 70)
    humicol = 0x0AFF;
  else if (huminum > 40)
    humicol = 0x0F0F;
  else if (huminum > 20)
    humicol = 0xFF0F;
  else
    humicol = 0xF00F;
  humidityWin();

  // 城市名称
  clk.createSprite(94, 30);
  clk.fillSprite(bgColor);
  clk.setTextDatum(CC_DATUM);
  clk.setTextColor(TFT_WHITE, bgColor);
  clk.drawString(sk["cityname"].as<String>(), 44, 16);
  clk.pushSprite(15, 15);
  clk.deleteSprite();

  // PM2.5空气指数
  uint16_t pm25BgColor = tft.color565(156, 202, 127); // 优
  String aqiTxt = "优";
  int pm25V = sk["aqi"];
  if (pm25V > 200) {
    pm25BgColor = tft.color565(136, 11, 32); // 重度
    aqiTxt = "重度";
  } else if (pm25V > 150) {
    pm25BgColor = tft.color565(186, 55, 121); // 中度
    aqiTxt = "中度";
  } else if (pm25V > 100) {
    pm25BgColor = tft.color565(242, 159, 57); // 轻
    aqiTxt = "轻度";
  } else if (pm25V > 50) {
    pm25BgColor = tft.color565(247, 219, 100); // 良
    aqiTxt = "良";
  }
  clk.createSprite(56, 24);
  clk.fillSprite(bgColor);
  clk.fillRoundRect(0, 0, 50, 24, 4, pm25BgColor);
  clk.setTextDatum(CC_DATUM);
  clk.setTextColor(0x0000);
  clk.drawString(aqiTxt, 25, 13);
  clk.pushSprite(104, 18);
  clk.deleteSprite();

  scrollText[0] = "实时天气 " + sk["weather"].as<String>();
  scrollText[1] = "空气质量 " + aqiTxt;
  scrollText[2] = "风向 " + sk["WD"].as<String>() + sk["WS"].as<String>();
  // 天气图标
  wrat.printfweather(
      170, 15, atoi((sk["weathercode"].as<String>()).substring(1, 3).c_str()));
  // 左上角滚动字幕
  // 解析第二段JSON
  deserializeJson(doc, *cityDZ);
  JsonObject dz = doc.as<JsonObject>();
  scrollText[3] = "今日" + dz["weather"].as<String>();
  deserializeJson(doc, *dataFC);
  JsonObject fc = doc.as<JsonObject>();
  scrollText[4] = "最低温度" + fc["fd"].as<String>() + "℃";
  scrollText[5] = "最高温度" + fc["fc"].as<String>() + "℃";
  scrollText[6] = WiFi.localIP().toString();
  clk.unloadFont();
}

int currentIndex = 0;
TFT_eSprite clkb = TFT_eSprite(&tft);

void scrollBanner() {
  if (scrollText[currentIndex]) {
    clkb.setColorDepth(8);
    clkb.loadFont(ZoloFont_20);
    clkb.createSprite(160, 30);
    clkb.fillSprite(bgColor);
    clkb.setTextWrap(false);
    clkb.setTextDatum(CC_DATUM);
    clkb.setTextColor(TFT_WHITE, bgColor);
    clkb.drawString(scrollText[currentIndex], 74, 16);
    clkb.pushSprite(6, 45);
    clkb.fillSprite(bgColor);
    clkb.deleteSprite();
    clkb.unloadFont();
    currentIndex = currentIndex >= 6 ? 0 : currentIndex + 1;
  }
  prevTime = 1;
}

#if imgAst_EN

void imgAnim() {
  int x = 160, y = 160;
  // x ms切换一次
  if (millis() - AprevTime > 120) {
    Anim++;
    AprevTime = millis();
  }
  if (Anim == 10)
    Anim = 0;
  switch (Anim) {
  case 0:
    TJpgDec.drawJpg(x, y, i0, sizeof(i0));
    break;
  case 1:
    TJpgDec.drawJpg(x, y, i1, sizeof(i1));
    break;
  case 2:
    TJpgDec.drawJpg(x, y, i2, sizeof(i2));
    break;
  case 3:
    TJpgDec.drawJpg(x, y, i3, sizeof(i3));
    break;
  case 4:
    TJpgDec.drawJpg(x, y, i4, sizeof(i4));
    break;
  case 5:
    TJpgDec.drawJpg(x, y, i5, sizeof(i5));
    break;
  case 6:
    TJpgDec.drawJpg(x, y, i6, sizeof(i6));
    break;
  case 7:
    TJpgDec.drawJpg(x, y, i7, sizeof(i7));
    break;
  case 8:
    TJpgDec.drawJpg(x, y, i8, sizeof(i8));
    break;
  case 9:
    TJpgDec.drawJpg(x, y, i9, sizeof(i9));
    break;
  default:
    Serial.println("显示Anim错误");
    break;
  }
}

#endif

unsigned char Hour_sign = 60;
unsigned char Minute_sign = 60;
unsigned char Second_sign = 60;

void digitalClockDisplay(int reflash_en) {
  struct tm info;
  getLocalTime(&info);

  int timey = 82;
  // 时钟刷新
  if (info.tm_hour != Hour_sign || reflash_en == 1) {
    dig.printfW3660(20, timey, info.tm_hour / 10);
    dig.printfW3660(60, timey, info.tm_hour % 10);
    Hour_sign = info.tm_hour;
  }
  // 分钟刷新
  if (info.tm_min != Minute_sign || reflash_en == 1) {
    dig.printfO3660(101, timey, info.tm_min / 10);
    dig.printfO3660(141, timey, info.tm_min % 10);
    Minute_sign = info.tm_min;
  }
  // 分钟刷新
  if (info.tm_sec != Second_sign || reflash_en == 1) {
    dig.printfW1830(182, timey + 30, info.tm_sec / 10);
    dig.printfW1830(202, timey + 30, info.tm_sec % 10);
    Second_sign = info.tm_sec;
  }

  if (reflash_en == 1)
    reflash_en = 0;
  /***日期****/
  clk.setColorDepth(8);
  clk.loadFont(ZoloFont_20);

  // 星期
  clk.createSprite(58, 30);
  clk.fillSprite(bgColor);
  clk.setTextDatum(CC_DATUM);
  clk.setTextColor(TFT_WHITE, bgColor);
  String wk[7] = {"日", "一", "二", "三", "四", "五", "六"};
  String s = "周" + wk[info.tm_wday];
  clk.drawString(s, 29, 16);
  clk.pushSprite(102, 150);
  clk.deleteSprite();

  // 月日
  clk.createSprite(95, 30);
  clk.fillSprite(bgColor);
  clk.setTextDatum(CC_DATUM);
  clk.setTextColor(TFT_WHITE, bgColor);
  s = String(info.tm_mon + 1);
  s = s + "月" + info.tm_mday + "日";
  clk.drawString(s, 49, 16);
  clk.pushSprite(5, 150);
  clk.deleteSprite();
  clk.unloadFont();
}
