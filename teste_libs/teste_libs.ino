// Teste de diagnóstico — inclui as mesmas bibliotecas que o pap.ino
// e verifica se o ecrã ainda funciona

#define BLYNK_TEMPLATE_ID   "TMPL5mbVhAiea"
#define BLYNK_TEMPLATE_NAME "LED ESP32"
#define BLYNK_PRINT         Serial

#include <BlynkSimpleEsp32.h>
#include <EEPROM.h>
#include <HX711.h>
#include <SPI.h>
#include <Stepper.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WidgetRTC.h>
#include <esp_wifi.h>

TFT_eSPI tft = TFT_eSPI();

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("Setup iniciado!");

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.println("Libs OK!");

  Serial.println("TFT inicializado com sucesso!");
}

void loop() {
  // nada
}
