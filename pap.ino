#define BLYNK_TEMPLATE_ID "TMPL5mbVhAiea"
#define BLYNK_TEMPLATE_NAME "LED ESP32"
#define BLYNK_PRINT Serial

#include <BlynkSimpleEsp32.h>
#include <EEPROM.h>
#include <HX711.h>
#include <SPI.h>
#include <Stepper.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <WiFiManager.h> // Configuração Wi-Fi dinâmica
#include <WiFiClient.h>
#include <WidgetRTC.h>
#include <esp_wifi.h>

// --- Credenciais ---
char auth[] = "sOOvSR9kmZxS_CMp5pvbqQHDbGvJczP1";
char ssid[] = "NOS-3CF6";
char pass[] = "G4HWAXEU";

// --- Pinos ---
#define TRIG_GERAL 33 // Depósito 1
#define ECHO_GERAL 26
#define TRIG_AGUA 32 // Depósito 2
#define ECHO_AGUA 35
#define MOTOR_IN1 13
#define MOTOR_IN2 14
#define MOTOR_IN3 12
#define MOTOR_IN4 27
#define HX711_DT 16
#define HX711_SCK 17
#define SENSOR_AGUA_PIN 34
#define BOMBA_GATE 25

// --- Constantes Sensor Ultrassónico ---
#define DIST_CHEIO 5
#define DIST_VAZIO 40
#define DIST_MAX 100
#define LEITURAS_MEDIA 5

// --- Motor ---
#define STEPPER_STEPS 2048
#define STEPPER_SPEED 15
#define PORCAO_STEPS 1024

// --- Alertas ---
#define NIVEL_CRITICO 20
#define NIVEL_BAIXO 60

// --- Célula de Carga ---
#define FATOR_CALIBRACAO 420.0f
#define TARA_AUTOMATICA true

// --- Sensor de Água ---
#define AGUA_LIMIAR_BAIXO 1000
#define AGUA_LIMIAR_CHEIO 3000
#define AGUA_DEBOUNCE 3
#define TEMPO_BOMBA_MAX 10000

// --- Temporizadores ---
#define INTERVALO_SENSOR 2000
#define INTERVALO_ALERTA 30000

// --- EEPROM ---
#define EEPROM_SIZE 64
#define EEPROM_ADDR_H1H 0    // Horário 1 hora
#define EEPROM_ADDR_H1M 1    // Horário 1 minuto
#define EEPROM_ADDR_H2H 2    // Horário 2 hora
#define EEPROM_ADDR_H2M 3    // Horário 2 minuto
#define EEPROM_VALID_FLAG 42 // Valor mágico para validar EEPROM

// --- Pinos Virtuais Blynk ---
#define VPIN_BOTAO V1
#define VPIN_NIVEL_RACAO1 V2
#define VPIN_NIVEL_RACAO2 V3
#define VPIN_PESO V4
#define VPIN_AGUA V5
#define VPIN_TARA V6
#define VPIN_HORARIO1 V7
#define VPIN_HORARIO2 V8
#define VPIN_BOMBA_MANUAL V9 // Botão manual da bomba no Blynk
#define VPIN_WIFI_LIST V10   // Lista de SSIDs separados por vírgula (Envio)
#define VPIN_WIFI_SCAN_TRIGGER V11 // Pedido de scan via painel (Recibo)

// --- Objetos ---
Stepper myStepper(STEPPER_STEPS, MOTOR_IN1, MOTOR_IN2, MOTOR_IN3,
                  MOTOR_IN4);
TFT_eSPI tft = TFT_eSPI();
WidgetRTC rtc;
BlynkTimer timer;
HX711 balanca;

// --- Histórico ---
#define MAX_HISTORICO 5
struct Refeicao {
  int hora, minuto;
  float peso;
};
Refeicao historico[MAX_HISTORICO];
int totalRefeicoes = 0;

// --- Horários ---
int horario1H = -1, horario1M = -1;
int horario2H = -1, horario2M = -1;

// --- Estado Global ---
struct Estado {
  int percRacao1 = -1;
  int percRacao2 = -1;
  float pesoAtual = 0.0f;
  int nivelAgua = 0;
  int nivelAguaAnterior = 0;
  int nivelAguaContador = 0;
  int ultimaH = 0;
  int ultimaM = 0;
  bool alimentando = false;
  bool bombaAtiva = false;
  unsigned long tempoBomba = 0;
  bool online = false;
  bool alertaRacao1 = false;
  bool alertaRacao2 = false;
  bool alertaAgua = false;
  unsigned long ultimoAlertaRacao1 = 0;
  unsigned long ultimoAlertaRacao2 = 0;
  unsigned long ultimoAlertaAgua = 0;
} estado;

// =====================================================================
// EEPROM — Guardar e carregar horários
// =====================================================================

void eepromGuardar() {
  EEPROM.write(EEPROM_ADDR_H1H, (uint8_t)(horario1H < 0 ? 255 : horario1H));
  EEPROM.write(EEPROM_ADDR_H1M, (uint8_t)(horario1M < 0 ? 255 : horario1M));
  EEPROM.write(EEPROM_ADDR_H2H, (uint8_t)(horario2H < 0 ? 255 : horario2H));
  EEPROM.write(EEPROM_ADDR_H2M, (uint8_t)(horario2M < 0 ? 255 : horario2M));
  EEPROM.write(4, EEPROM_VALID_FLAG); // flag de validade
  EEPROM.commit();
  Serial.println("[EEPROM] Horários guardados.");
}

void eepromCarregar() {
  uint8_t flag = EEPROM.read(4);
  if (flag != EEPROM_VALID_FLAG) {
    Serial.println("[EEPROM] Sem dados válidos — a usar defaults.");
    return;
  }
  uint8_t h1h = EEPROM.read(EEPROM_ADDR_H1H);
  uint8_t h1m = EEPROM.read(EEPROM_ADDR_H1M);
  uint8_t h2h = EEPROM.read(EEPROM_ADDR_H2H);
  uint8_t h2m = EEPROM.read(EEPROM_ADDR_H2M);

  horario1H = (h1h == 255) ? -1 : h1h;
  horario1M = (h1m == 255) ? -1 : h1m;
  horario2H = (h2h == 255) ? -1 : h2h;
  horario2M = (h2m == 255) ? -1 : h2m;

  Serial.printf("[EEPROM] H1=%02d:%02d  H2=%02d:%02d\n", horario1H, horario1M,
                horario2H, horario2M);
}

// =====================================================================
// UTILITÁRIOS
// =====================================================================

const char *labelAgua(int n) {
  switch (n) {
  case 0:
    return "SEM AGUA";
  case 1:
    return "BAIXO   ";
  case 2:
    return "OK      ";
  default:
    return "CHEIO   ";
  }
}

uint16_t corAgua(int n) {
  switch (n) {
  case 0:
    return TFT_RED;
  case 1:
    return TFT_ORANGE;
  case 2:
    return TFT_GREEN;
  default:
    return TFT_CYAN;
  }
}

uint16_t corPorNivel(int p) {
  if (p < NIVEL_CRITICO)
    return TFT_RED;
  if (p < NIVEL_BAIXO)
    return TFT_YELLOW;
  return TFT_GREEN;
}

// =====================================================================
// SENSORES ULTRASSÓNICOS
// =====================================================================

int lerDistanciaRaw(int trigPin, int echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  long dur = pulseIn(echoPin, HIGH, 30000);
  return (dur == 0) ? -1 : (int)(dur * 0.034f / 2);
}

int lerDistanciaFiltrada(int trigPin, int echoPin) {
  int soma = 0, validas = 0;
  for (int i = 0; i < LEITURAS_MEDIA; i++) {
    int d = lerDistanciaRaw(trigPin, echoPin);
    if (d > 0 && d < DIST_MAX) {
      soma += d;
      validas++;
    }
    delay(10);
  }
  return (validas > 0) ? (soma / validas) : -1;
}

// =====================================================================
// CÉLULA DE CARGA
// =====================================================================

float lerPeso() {
  if (!balanca.is_ready())
    return -1.0f;
  float raw = balanca.get_units(5);
  return (raw < 0) ? 0 : raw;
}

void realizarTara() {
  if (!balanca.is_ready())
    return;
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString("TARA...", 10, 355, 2);
  balanca.tare(10);
  tft.fillRect(0, 352, 290, 22, TFT_BLACK);
  Serial.println("[Balanca] Tara feita.");
}

// =====================================================================
// SENSOR DE ÁGUA
// =====================================================================

int lerNivelAgua() {
  int leituras[5];
  for (int i = 0; i < 5; i++) {
    leituras[i] = analogRead(SENSOR_AGUA_PIN);
    delay(10);
  }
  for (int i = 0; i < 4; i++)
    for (int j = i + 1; j < 5; j++)
      if (leituras[i] > leituras[j]) {
        int tmp = leituras[i];
        leituras[i] = leituras[j];
        leituras[j] = tmp;
      }
  int raw = leituras[2];
  Serial.printf("[Agua] ADC mediana: %d\n", raw);
  if (raw < AGUA_LIMIAR_BAIXO)
    return 0;
  if (raw < AGUA_LIMIAR_BAIXO + 500)
    return 1;
  if (raw < AGUA_LIMIAR_CHEIO)
    return 2;
  return 3;
}

// =====================================================================
// BOMBA DE ÁGUA
// =====================================================================

void controlarBomba(int nivelAgua) {
  unsigned long agora = millis();

  // Liga se nível baixo ou sem água
  if (nivelAgua <= 1 && !estado.bombaAtiva) {
    digitalWrite(BOMBA_GATE, HIGH);
    estado.bombaAtiva = true;
    estado.tempoBomba = agora;
    Serial.println("[Bomba] LIGADA");
    if (Blynk.connected())
      Blynk.logEvent("bomba_ligada", "Bomba de agua ativada!");
  }

  // Desliga se nível reposto ou timeout de segurança
  if (estado.bombaAtiva &&
      (nivelAgua > 1 || agora - estado.tempoBomba > TEMPO_BOMBA_MAX)) {
    digitalWrite(BOMBA_GATE, LOW);
    estado.bombaAtiva = false;
    Serial.println("[Bomba] DESLIGADA");
  }
}

// =====================================================================
// DISPLAY — INTERFACE COM DOIS DEPÓSITOS
// =====================================================================

void atualizarHorariosEcra() {
  tft.fillRect(0, 335, 295, 22, TFT_BLACK);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  char h1[8], h2[8];
  if (horario1H >= 0)
    sprintf(h1, "%02d:%02d", horario1H, horario1M);
  else
    sprintf(h1, "--:--");
  if (horario2H >= 0)
    sprintf(h2, "%02d:%02d", horario2H, horario2M);
  else
    sprintf(h2, "--:--");
  char buf[30];
  sprintf(buf, "%s  |  %s", h1, h2);
  tft.drawString(buf, 10, 338, 2);
}

void desenharInterfaceBase() {
  tft.fillScreen(TFT_BLACK);

  // Barra de topo
  tft.fillRect(0, 0, 480, 42, tft.color565(25, 25, 25));
  tft.setTextColor(TFT_CYAN, tft.color565(25, 25, 25));
  tft.setCursor(80, 10);
  tft.setTextFont(4);
  tft.print("SMART PET FEEDER");

  // Labels coluna esquerda
  tft.setTextColor(tft.color565(130, 130, 130), TFT_BLACK);
  tft.drawString("PESO TIGELA", 10, 50, 2);
  tft.drawString("NIVEL AGUA", 10, 115, 2);
  tft.drawString("ULT. REFEIC.", 10, 180, 2);
  tft.drawString("HORARIOS", 10, 320, 2);

  // Linhas divisórias coluna esquerda
  tft.drawFastHLine(5, 105, 285, tft.color565(50, 50, 50));
  tft.drawFastHLine(5, 170, 285, tft.color565(50, 50, 50));
  tft.drawFastHLine(5, 230, 285, tft.color565(50, 50, 50));
  tft.drawFastHLine(5, 313, 285, tft.color565(50, 50, 50));

  // ── DOIS TANQUES lado a lado ──────────────────────────────────────
  // Tanque 1 (esquerda da zona direita)
  tft.drawRoundRect(295, 48, 85, 272, 8, tft.color565(80, 80, 80));
  tft.setTextColor(tft.color565(130, 130, 130), TFT_BLACK);
  tft.drawString("DEP1", 313, 53, 2);

  // Tanque 2 (direita da zona direita)
  tft.drawRoundRect(390, 48, 85, 272, 8, tft.color565(80, 80, 80));
  tft.drawString("DEP2", 408, 53, 2);

  // Marcas de escala em ambos os tanques
  for (int p = 25; p <= 75; p += 25) {
    int y = map(p, 0, 100, 314, 70);
    tft.drawFastHLine(296, y, 8, tft.color565(80, 80, 80));
    tft.drawFastHLine(391, y, 8, tft.color565(80, 80, 80));
  }

  atualizarHorariosEcra();
}

void atualizarTanque(int x, int w, int p, int tanqueNum) {
  uint16_t cor = corPorNivel(p);
  int innerX = x + 5;
  int innerW = w - 10;
  int topY = 70;
  int bottomY = 315;
  int altura = bottomY - topY;
  int barH = map(p, 0, 100, 0, altura);

  // Limpa área vazia (topo)
  tft.fillRect(innerX, topY, innerW, altura - barH, TFT_BLACK);
  // Barra colorida (baixo)
  if (barH > 0)
    tft.fillRect(innerX, bottomY - barH, innerW, barH, cor);

  // Percentagem dentro do tanque
  tft.fillRect(innerX, bottomY - barH + 2, innerW, 18, cor);
  if (barH > 20) {
    tft.setTextColor(TFT_BLACK, cor);
    char buf[5];
    sprintf(buf, "%d%%", p);
    int tw = tft.textWidth(buf, 1);
    tft.drawString(buf, x + (w - tw) / 2, bottomY - barH + 4, 1);
  }
}

void atualizarNivelRacaoEcra(int p1, int p2) {
  atualizarTanque(295, 85, p1, 1);
  atualizarTanque(390, 85, p2, 2);
}

void atualizarPeso(float peso) {
  tft.fillRect(10, 62, 280, 36, TFT_BLACK);
  char buf[14];
  sprintf(buf, (peso < 0) ? "  ----  " : "%6.1f g", peso);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(buf, 10, 65, 4);
}

void atualizarAgua(int nivel) {
  uint16_t cor = corAgua(nivel);
  tft.fillRect(10, 127, 280, 36, TFT_BLACK);
  tft.setTextColor(cor, TFT_BLACK);
  tft.drawString(labelAgua(nivel), 10, 130, 4);

  // Ícone bomba ativa
  if (estado.bombaAtiva) {
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString("BOMBA ON", 160, 130, 4);
  }
}

void atualizarHoraRefeicao() {
  char buf[14];
  if (estado.ultimaH == 0 && estado.ultimaM == 0)
    sprintf(buf, "  --:--");
  else
    sprintf(buf, "  %02d:%02d h", estado.ultimaH, estado.ultimaM);
  tft.fillRect(10, 192, 280, 32, TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(buf, 10, 195, 4);
}

void atualizarStatusWifi(bool online) {
  uint16_t bg = online ? tft.color565(0, 70, 0) : tft.color565(70, 0, 0);
  uint16_t fg = online ? TFT_GREEN : TFT_RED;
  tft.fillRect(358, 10, 115, 22, bg);
  tft.setTextColor(fg, bg);
  tft.drawString(online ? " ONLINE " : " OFFLINE", 362, 14, 2);
}

// =====================================================================
// HISTÓRICO
// =====================================================================

void registarRefeicao(float peso) {
  for (int i = MAX_HISTORICO - 1; i > 0; i--)
    historico[i] = historico[i - 1];
  historico[0] = {hour(), minute(), peso};
  if (totalRefeicoes < MAX_HISTORICO)
    totalRefeicoes++;
}

// =====================================================================
// ALIMENTAÇÃO
// =====================================================================

void darComida() {
  if (estado.alimentando)
    return;
  estado.alimentando = true;

  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.drawString("A DEITAR COMIDA...", 10, 355, 2);

  myStepper.setSpeed(STEPPER_SPEED);
  myStepper.step(PORCAO_STEPS);
  digitalWrite(MOTOR_IN1, LOW);
  digitalWrite(MOTOR_IN2, LOW);
  digitalWrite(MOTOR_IN3, LOW);
  digitalWrite(MOTOR_IN4, LOW);

  estado.ultimaH = hour();
  estado.ultimaM = minute();
  registarRefeicao(estado.pesoAtual);
  atualizarHoraRefeicao();

  tft.fillRect(0, 352, 290, 22, TFT_BLACK);
  estado.alimentando = false;
}

// =====================================================================
// HORÁRIOS
// =====================================================================

void verificarHorarios() {
  if (horario1H < 0 && horario2H < 0)
    return;

  int h = hour();
  int m = minute();
  int s = second();

  Serial.printf("[Horarios] %02d:%02d:%02d | H1=%02d:%02d | H2=%02d:%02d\n", h,
                m, s, horario1H, horario1M, horario2H, horario2M);

  if (s > 30)
    return;

  static int ultimoH1 = -1;
  static int ultimoH2 = -1;

  if (horario1H >= 0 && h == horario1H && m == horario1M && ultimoH1 != m) {
    Serial.println("[Horario 1] A dar comida!");
    ultimoH1 = m;
    darComida();
  }
  if (horario2H >= 0 && h == horario2H && m == horario2M && ultimoH2 != m) {
    Serial.println("[Horario 2] A dar comida!");
    ultimoH2 = m;
    darComida();
  }
}

// =====================================================================
// BLYNK
// =====================================================================

BLYNK_CONNECTED() {
  rtc.begin();
  Blynk.syncAll();
}

BLYNK_WRITE(V1) {
  if (param.asInt() == 1)
    darComida();
}

BLYNK_WRITE(V6) {
  if (param.asInt() == 1)
    realizarTara();
}

BLYNK_WRITE(V7) {
  TimeInputParam t(param);
  if (t.hasStartTime()) {
    horario1H = t.getStartHour();
    horario1M = t.getStartMinute();
    Serial.printf("[Horario 1] %02d:%02d\n", horario1H, horario1M);
    eepromGuardar();
    atualizarHorariosEcra();
  }
}

BLYNK_WRITE(V8) {
  TimeInputParam t(param);
  if (t.hasStartTime()) {
    horario2H = t.getStartHour();
    horario2M = t.getStartMinute();
    Serial.printf("[Horario 2] %02d:%02d\n", horario2H, horario2M);
    eepromGuardar();
    atualizarHorariosEcra();
  }
}

// Botão manual da bomba no Blynk
BLYNK_WRITE(V9) {
  if (param.asInt() == 1) {
    digitalWrite(BOMBA_GATE, HIGH);
    estado.bombaAtiva = true;
    estado.tempoBomba = millis();
    Serial.println("[Bomba] Ativada manualmente via Blynk");
  } else {
    digitalWrite(BOMBA_GATE, LOW);
    estado.bombaAtiva = false;
    Serial.println("[Bomba] Desativada manualmente via Blynk");
  }
}

// Escaneamento de redes Wi-Fi locais pedido pelo painel
void escaniarWifi() {
  Serial.println("[WiFi] A escaniar redes 2.4GHz...");
  int n = WiFi.scanNetworks();
  String lista = "";
  if (n > 0) {
    int limit = min(5, n); // Limita às 5 melhores redes
    for (int i = 0; i < limit; ++i) {
      lista += WiFi.SSID(i);
      if (i < limit - 1) lista += ",";
    }
  } else {
    lista = "Nenhuma rede detetada";
  }
  Blynk.virtualWrite(V10, lista);
  Serial.println("[WiFi] Redes enviadas para o Blynk: " + lista);
}

// Receptor de gatilho para escaneamento de redes (V11)
BLYNK_WRITE(V11) {
  if (param.asInt() == 1) {
    escaniarWifi();
  }
}

// =====================================================================
// TAREFA SENSORES
// =====================================================================

void tarefaSensores() {
  unsigned long agora = millis();

  // --- 1. Depósito 1 (ultrassónico 1) ---
  int dist1 = lerDistanciaFiltrada(TRIG_GERAL, ECHO_GERAL);
  if (dist1 > 0 && dist1 < DIST_MAX) {
    int novaPerc1 =
        constrain(map(dist1, DIST_CHEIO, DIST_VAZIO, 100, 0), 0, 100);
    if (novaPerc1 != estado.percRacao1) {
      estado.percRacao1 = novaPerc1;
      atualizarNivelRacaoEcra(estado.percRacao1,
                              estado.percRacao2 < 0 ? 0 : estado.percRacao2);
      if (Blynk.connected())
        Blynk.virtualWrite(VPIN_NIVEL_RACAO1, estado.percRacao1);
    }
    // Alerta depósito 1
    if (estado.percRacao1 < NIVEL_CRITICO && Blynk.connected()) {
      if (!estado.alertaRacao1 ||
          agora - estado.ultimoAlertaRacao1 > INTERVALO_ALERTA) {
        Blynk.logEvent("nivel_baixo", "Deposito 1: nivel de racao critico!");
        estado.alertaRacao1 = true;
        estado.ultimoAlertaRacao1 = agora;
      }
    } else
      estado.alertaRacao1 = false;
  }

  // --- 2. Depósito 2 (ultrassónico 2) ---
  int dist2 = lerDistanciaFiltrada(TRIG_AGUA, ECHO_AGUA);
  if (dist2 > 0 && dist2 < DIST_MAX) {
    int novaPerc2 =
        constrain(map(dist2, DIST_CHEIO, DIST_VAZIO, 100, 0), 0, 100);
    if (novaPerc2 != estado.percRacao2) {
      estado.percRacao2 = novaPerc2;
      atualizarNivelRacaoEcra(estado.percRacao1 < 0 ? 0 : estado.percRacao1,
                              estado.percRacao2);
      if (Blynk.connected())
        Blynk.virtualWrite(VPIN_NIVEL_RACAO2, estado.percRacao2);
    }
    // Alerta depósito 2
    if (estado.percRacao2 < NIVEL_CRITICO && Blynk.connected()) {
      if (!estado.alertaRacao2 ||
          agora - estado.ultimoAlertaRacao2 > INTERVALO_ALERTA) {
        Blynk.logEvent("nivel_baixo2", "Deposito 2: nivel de racao critico!");
        estado.alertaRacao2 = true;
        estado.ultimoAlertaRacao2 = agora;
      }
    } else
      estado.alertaRacao2 = false;
  }

  // --- 3. Célula de carga ---
  float peso = lerPeso();
  if (peso >= 0 && abs(peso - estado.pesoAtual) > 0.5f) {
    estado.pesoAtual = peso;
    atualizarPeso(estado.pesoAtual);
    if (Blynk.connected())
      Blynk.virtualWrite(VPIN_PESO, estado.pesoAtual);
  }

  // --- 4. Delay ADC ---
  delay(50);

  // --- 5. Sensor de água com debounce ---
  int novoNivelAgua = lerNivelAgua();
  if (novoNivelAgua == estado.nivelAguaAnterior) {
    estado.nivelAguaContador++;
  } else {
    estado.nivelAguaContador = 0;
    estado.nivelAguaAnterior = novoNivelAgua;
  }
  if (estado.nivelAguaContador >= AGUA_DEBOUNCE &&
      novoNivelAgua != estado.nivelAgua) {
    estado.nivelAgua = novoNivelAgua;
    atualizarAgua(estado.nivelAgua);
    controlarBomba(estado.nivelAgua);
    if (Blynk.connected())
      Blynk.virtualWrite(VPIN_AGUA, estado.nivelAgua);
  }

  // Verificar timeout da bomba mesmo sem mudança de nível
  if (estado.bombaAtiva && millis() - estado.tempoBomba > TEMPO_BOMBA_MAX) {
    digitalWrite(BOMBA_GATE, LOW);
    estado.bombaAtiva = false;
    Serial.println("[Bomba] Timeout de segurança");
  }

  // Alerta água
  if (estado.nivelAgua == 0 && Blynk.connected()) {
    if (!estado.alertaAgua ||
        agora - estado.ultimoAlertaAgua > INTERVALO_ALERTA) {
      Blynk.logEvent("sem_agua", "Tigela de agua vazia!");
      estado.alertaAgua = true;
      estado.ultimoAlertaAgua = agora;
    }
  } else
    estado.alertaAgua = false;

  // --- 6. Wi-Fi ---
  bool onlineAgora = Blynk.connected();
  if (onlineAgora != estado.online) {
    estado.online = onlineAgora;
    atualizarStatusWifi(estado.online);
  }
}

// =====================================================================
// SETUP & LOOP
// =====================================================================

void setup() {
  Serial.begin(115200);
  delay(1000);

  // EEPROM — carregar horários guardados (funciona sem Wi-Fi)
  EEPROM.begin(EEPROM_SIZE);
  eepromCarregar();

  // Motor
  myStepper.setSpeed(STEPPER_SPEED);
  pinMode(MOTOR_IN1, OUTPUT);
  pinMode(MOTOR_IN2, OUTPUT);
  pinMode(MOTOR_IN3, OUTPUT);
  pinMode(MOTOR_IN4, OUTPUT);

  // Sensores ultrassónicos
  pinMode(TRIG_GERAL, OUTPUT);
  pinMode(ECHO_GERAL, INPUT);
  pinMode(TRIG_AGUA, OUTPUT);
  pinMode(ECHO_AGUA, INPUT);

  // Sensor de água e bomba
  pinMode(SENSOR_AGUA_PIN, INPUT);
  pinMode(BOMBA_GATE, OUTPUT);
  digitalWrite(BOMBA_GATE, LOW);
  analogReadResolution(12);

  // Célula de carga
  balanca.begin(HX711_DT, HX711_SCK);
  balanca.set_scale(FATOR_CALIBRACAO);
  if (TARA_AUTOMATICA) {
    delay(500);
    balanca.tare(10);
  }

  // Display
  tft.init();
  tft.setRotation(1);
  desenharInterfaceBase();
  atualizarHoraRefeicao();
  atualizarStatusWifi(false);

  // Mostrar horários carregados da EEPROM
  atualizarHorariosEcra();

  // Wi-Fi
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString("A conectar...", 10, 355, 2);

  WiFi.persistent(false);
  WiFi.mode(WIFI_OFF);
  delay(500);
  WiFi.mode(WIFI_STA);
  delay(200);
  WiFi.begin(ssid, pass);

  int tentativas = 0;
  while (WiFi.status() != WL_CONNECTED && tentativas < 30) {
    delay(500);
    tentativas++;
    Serial.printf("Wi-Fi... tentativa %d\n", tentativas);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi OK! IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("WiFi FALHOU — modo offline ativo (horarios da EEPROM)");
  }

  tft.fillRect(0, 352, 290, 22, TFT_BLACK);

  Blynk.config(auth);
  Blynk.connect();

  timer.setInterval(INTERVALO_SENSOR, tarefaSensores);
  timer.setInterval(10000L, verificarHorarios);
}

void loop() {
  Blynk.run();
  timer.run();
}