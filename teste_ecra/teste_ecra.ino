#include <TFT_eSPI.h>
#include <SPI.h>

TFT_eSPI tft = TFT_eSPI();  // Usa as configurações do User_Setup.h

void setup(void) {
  Serial.begin(115200);
  Serial.println("A iniciar o ecrã...");

  tft.init();
  tft.setRotation(1); // Modo paisagem
  
  // Limpa o ecrã a preto
  tft.fillScreen(TFT_BLACK);
  
  // Texto de teste
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.println("Teste de Ecra OK!");
  
  tft.setTextColor(TFT_RED);
  tft.println("Cor Vermelha");
  
  tft.setTextColor(TFT_GREEN);
  tft.println("Cor Verde");
  
  tft.setTextColor(TFT_BLUE);
  tft.println("Cor Azul");
}

void loop() {
  // Faz piscar um quadrado amarelo para ver se o código está a correr
  tft.fillRect(100, 100, 50, 50, TFT_YELLOW);
  delay(1000);
  tft.fillRect(100, 100, 50, 50, TFT_BLACK);
  delay(1000);
}
