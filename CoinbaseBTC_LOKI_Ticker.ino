#include "ESP8266WiFi.h"
#include "WiFiClientSecure.h"
#include "ArduinoJson.h"
#include <MD_MAX72xx.h>
#include <SPI.h>
#include <stdio.h>
//++++++++++++++++++++
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>

WiFiManager wifiManager;
WiFiClientSecure  client;
//+++++++++++++++++++++

// Define the number of devices we have in the chain and the hardware interface
// NOTE: These pin numbers will probably not work with your hardware and may
// need to be adapted
#define HARDWARE_TYPE MD_MAX72XX::GENERIC_HW
#define MAX_DEVICES 4

#define CLK_PIN   D5 // or SCK
#define DATA_PIN  D7 // or MOSI
#define CS_PIN    D8 // or SS

// SPI hardware interface
MD_MAX72XX mx = MD_MAX72XX(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);

// We always wait a bit between updates of the display
#define  DELAYTIME  100  // in milliseconds

// Number of coins
#define TOTAL_COINS 2

const char* ssid = "HanseNet";
const char* password = "qweqweqweqweq";

const char* coinbaseHost = "api.pro.coinbase.com";
const char* coinbaseFingerprint = "B4 1C 06 53 0E 93 AB DA CF B3 59 7F 3B F7 42 14 D7 00 16 55";

const char* binanceHost = "api.binance.com";
const char* binanceFingerprint = "41 82 D2 BA 64 E3 36 F1 3C 5E 49 05 2A A0 AA CB D0 F7 2B B7";

const char* tradeogreHost = "tradeogre.com";
const char* tradeogreFingerprint = "65 79 B4 1D 2F BD 34 A6 51 81 20 B6 C3 07 98 78 99 D0 DD D2";

struct Coin {
  String url;
  String ticker; 
  bool isCoinbaseCoin;
};

struct Coin cryptoCoins[TOTAL_COINS]; 

void scrollText(char *p) {
    uint8_t charWidth;
    uint8_t cBuf[8];  // this should be ok for all built-in fonts
    mx.clear();
    
    while (*p != '\0')
    {
        charWidth = mx.getChar(*p++, sizeof(cBuf) / sizeof(cBuf[0]), cBuf);
        
        for (uint8_t i=0; i<=charWidth; i++)  // allow space between characters
        {
            mx.transform(MD_MAX72XX::TSL);
            if (i < charWidth)
                mx.setColumn(0, cBuf[i]);
            delay(DELAYTIME);
        }
    }
}

void connectToWIFI() {
    Serial.println();
    Serial.print("connecting to ");
    Serial.println(ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());

}


JsonObject& getJsonObject(String url, bool isCoinbaseCoin) {
    const size_t capacity = (isCoinbaseCoin) ? JSON_OBJECT_SIZE(7) + 252 : JSON_OBJECT_SIZE(8) + 288;
    DynamicJsonBuffer jsonBuffer(capacity);
    
    // Use WiFiClientSecure class to create TLS connection
    WiFiClientSecure client;
    client.setTimeout(10000);
    
    const char* host = (isCoinbaseCoin) ? coinbaseHost : tradeogreHost;
    const char* fingerprint = (isCoinbaseCoin) ? coinbaseFingerprint : tradeogreFingerprint;
    Serial.println(host);
    Serial.printf("Using fingerprint '%s'\n", fingerprint);
    client.setFingerprint(fingerprint);
    
    if (!client.connect(host, 443)) {
        Serial.println("connection failed");
        scrollText("Connection failed!");
        // No further work should be done if the connection failed
        return jsonBuffer.parseObject(client);
    }
    Serial.println(F("Connected!"));
    
    // Send HTTP Request
    String httpEnding = (isCoinbaseCoin) ? " HTTP/1.1\r\n" : " HTTP/1.0\r\n";
    client.print(String("GET ") + url + httpEnding +
                 "Host: " + host + "\r\n" +
                 "User-Agent: BuildFailureDetectorESP8266\r\n" +
                 "Connection: close\r\n\r\n");
    Serial.println("request sent");
    
    // Check HTTP Status
    char status[32] = {0};
    client.readBytesUntil('\r', status, sizeof(status));
    if (strcmp(status, "HTTP/1.1 200 OK") != 0) {
        Serial.print(F("Unexpected response: "));
        Serial.println(status);
        return jsonBuffer.parseObject(client);
    }
    
    // Skip HTTP headers
    char endOfHeaders[] = "\r\n\r\n";
    if (!client.find(endOfHeaders)) {
        Serial.println(F("Invalid response"));
        //scrollText("Invalid Response");
    }
    
    // Parse JSON object
    JsonObject& root = jsonBuffer.parseObject(client);
    if (!root.success()) {
        Serial.println(F("Parsing failed!"));
        //scrollText("JSON Parse Failed!");
    }
    
    // Disconnect
    client.stop();
    jsonBuffer.clear();
    return root;
}

float bitcoinPrice = 0.0;

float convertToUSD(float cryptoPrice) {
    if (bitcoinPrice == 0.0) { getBitcoinPrice(); }
    return bitcoinPrice * cryptoPrice;
}

void getBitcoinPrice() {
    JsonObject& root = getJsonObject("/api/v1/ticker/price?symbol=BTCUSDC", false);
    Serial.print("The Bitcoin price is:");
    Serial.println(root["price"].as<float>());
    bitcoinPrice = root["price"].as<float>();
}

void getCoinPrice(String url, String cryptoName, bool isCoinbaseCoin) {
    JsonObject& root = getJsonObject(url, isCoinbaseCoin);
    Serial.println("==========");
    Serial.println(F("Response:"));
    Serial.print("Symbol: ");
    Serial.println(root["symbol"].as<char*>());
    Serial.print("Price: ");
    Serial.println(root["price"].as<char*>());
    
    float cryptoPrice = root["price"].as<float>();
    cryptoPrice = (isCoinbaseCoin) ? cryptoPrice : convertToUSD(cryptoPrice);
    Serial.println(cryptoPrice);
    Serial.println("==========");
    String output = cryptoName + " $" + String(cryptoPrice);
    Serial.println(output);
    
    // Update the bitcoinPrice if the User requests the Bitcoin Price
    bitcoinPrice = (cryptoName == "BTC") ? cryptoPrice : bitcoinPrice;
    
    char *cstr = new char[output.length() + 1];
    strcpy(cstr, output.c_str());
    scrollText(cstr);
    delete [] cstr;
}

void configureCoins() { 
  Coin bitcoin = { .url = "/products/BTC-USD/ticker", .ticker = "BTC", .isCoinbaseCoin = true }; 
//  Coin ethereum = { .url = "/products/ETH-USD/ticker", .ticker = "ETH", .isCoinbaseCoin = true };
//  Coin litcoin = { .url = "/products/LTC-USD/ticker", .ticker = "LTC", .isCoinbaseCoin = true };
//  Coin bitcoinCash = { .url = "/products/BCH-USD/ticker", .ticker = "BCH", .isCoinbaseCoin = true };

  
  // Lets add in some Binanace Coins
//  Coin ethereumClassic = { .url = "/api/v1/ticker/price?symbol=ETCBTC", .ticker = "ETC", .isCoinbaseCoin = false }; 
  Coin monero = { .url = "/api/v1/ticker/price?symbol=XMRBTC", .ticker = "Monero", .isCoinbaseCoin = false };
  Coin loki = { .url = "/api/v1/ticker/BTC-LOKI", .ticker = "|LOKI", .isCoinbaseCoin = false };
//  Coin stellar = { .url = "/api/v1/ticker/price?symbol=XLMBTC", .ticker = "Stellar", .isCoinbaseCoin = false };
//  Coin iota = { .url = "/api/v1/ticker/price?symbol=IOTABTC", .ticker = "IOTA", .isCoinbaseCoin = false };
//  Coin dash = { .url = "/api/v1/ticker/price?symbol=DASHBTC", .ticker = "Dash", .isCoinbaseCoin = false }; 
//  Coin nano = { .url = "/api/v1/ticker/price?symbol=NANOBTC", .ticker = "NANO", .isCoinbaseCoin = false};

  cryptoCoins[0] =  bitcoin;
  cryptoCoins[1] =  loki;
  /*
  cryptoCoins[2] =  litcoin;
  cryptoCoins[3] =  bitcoinCash;
  cryptoCoins[4] = ethereumClassic;
  cryptoCoins[5] =  ethereum;
  cryptoCoins[6] =  stellar;
  cryptoCoins[7] =  iota;
  cryptoCoins[8] =  dash;
  cryptoCoins[9] =  nano;
*/
}

void getAllCoinPrices() {
  Coin currentCoin;
  for(int index=0; index < TOTAL_COINS; index++) {
    currentCoin = cryptoCoins[index];
    getCoinPrice(currentCoin.url, currentCoin.ticker, currentCoin.isCoinbaseCoin);
  }
}

void setup() {
    mx.begin();
    Serial.begin(115200);
    connectToWIFI();
    configureCoins();
}

void loop() {
    getAllCoinPrices();
}
