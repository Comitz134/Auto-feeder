// =============================================================
// SMART PET FEEDER — Versão Melhorada
// Rafael Esteves Nunes & Rodrigo Guimarães | Colégio de Gaia 2026
// =============================================================

// ─── Identificação Blynk ─────────────────────────────────────
#define BLYNK_TEMPLATE_ID   "TMPL5mbVhAiea"
#define BLYNK_TEMPLATE_NAME "LED ESP32"
#define BLYNK_PRINT         Serial

// ─── Bibliotecas ─────────────────────────────────────────────
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

// =============================================================
// CREDENCIAIS  ← Alterar conforme necessário
// =============================================================
const char AUTH[] = "sOOvSR9kmZxS_CMp5pvbqQHDbGvJczP1";
const char SSID[] = "NOS-3CF6";
const char PASS[] = "G4HWAXEU";

// =============================================================
// PINOS
// =============================================================
// Sensores ultrassónicos
constexpr uint8_t TRIG_DEP1   = 33;
constexpr uint8_t ECHO_DEP1   = 26;
constexpr uint8_t TRIG_DEP2   = 32;
constexpr uint8_t ECHO_DEP2   = 35;

// Motor de passo
constexpr uint8_t MOTOR_IN1   = 13;
constexpr uint8_t MOTOR_IN2   = 14;
constexpr uint8_t MOTOR_IN3   = 12;
constexpr uint8_t MOTOR_IN4   = 27;

// Célula de carga
constexpr uint8_t HX711_DT    = 16;
constexpr uint8_t HX711_SCK   = 17;

// Sensor de água e bomba
constexpr uint8_t SENSOR_AGUA = 34;  // input-only, sem pull-up interno
constexpr uint8_t BOMBA_PIN   = 25;

// =============================================================
// CONSTANTES DE CONFIGURAÇÃO
// =============================================================
// Sensor ultrassónico (cm)
constexpr int DIST_CHEIO        = 5;
constexpr int DIST_VAZIO        = 40;
constexpr int DIST_INVALIDA     = 100;
constexpr int LEITURAS_MEDIA    = 5;

// Motor
constexpr int STEPPER_STEPS     = 2048;
constexpr int STEPPER_SPEED_RPM = 15;
constexpr int PORCAO_STEPS      = 1024;   // meia volta = 1 porção

// Alertas de nível de ração (%)
constexpr int NIVEL_CRITICO     = 20;
constexpr int NIVEL_BAIXO       = 60;

// Célula de carga
constexpr float FATOR_CALIBRACAO = 420.0f;
constexpr bool  TARA_NO_BOOT     = true;

// Sensor de água (valores ADC 0–4095)
constexpr int AGUA_LIM_VAZIO    = 500;   // abaixo → SEM ÁGUA
constexpr int AGUA_LIM_BAIXO    = 1500;  // abaixo → BAIXO
constexpr int AGUA_LIM_OK       = 3000;  // abaixo → OK, acima → CHEIO
constexpr int AGUA_DEBOUNCE     = 3;     // leituras consecutivas para confirmar
constexpr uint32_t BOMBA_TIMEOUT_MS = 10000UL;

// Temporizadores
constexpr uint32_t INTERVALO_SENSOR_MS  = 2000UL;
constexpr uint32_t INTERVALO_HORARIO_MS = 10000UL;
constexpr uint32_t INTERVALO_ALERTA_MS  = 30000UL;

// EEPROM
constexpr int EEPROM_SIZE      = 64;
constexpr uint8_t EEPROM_MAGIC = 0x42;   // valor mágico de validação
constexpr int ADDR_H1H         = 0;
constexpr int ADDR_H1M         = 1;
constexpr int ADDR_H2H         = 2;
constexpr int ADDR_H2M         = 3;
constexpr int ADDR_MAGIC       = 4;

// Histórico
constexpr int MAX_HISTORICO    = 5;

// Pinos Virtuais Blynk
constexpr int VP_ALIMENTAR     = V1;
constexpr int VP_DEP1          = V2;
constexpr int VP_DEP2          = V3;
constexpr int VP_PESO          = V4;
constexpr int VP_AGUA          = V5;
constexpr int VP_TARA          = V6;
constexpr int VP_HORARIO1      = V7;
constexpr int VP_HORARIO2      = V8;
constexpr int VP_BOMBA_MANUAL  = V9;
constexpr int VP_WIFI_REDES    = V10;
constexpr int VP_WIFI_SCAN     = V11;

// =============================================================
// OBJECTOS GLOBAIS
// =============================================================
Stepper* motor = nullptr;  // Inicializado em setup() para evitar crash no arranque
TFT_eSPI tft;
WidgetRTC rtc;
BlynkTimer timer;
HX711 balanca;

// =============================================================
// ESTRUTURAS DE DADOS
// =============================================================
struct Refeicao {
  uint8_t hora;
  uint8_t minuto;
  float   peso;
};

struct EstadoSistema {
  // Sensores
  int   dep1Perc        = -1;
  int   dep2Perc        = -1;
  float pesoAtual       = 0.0f;
  int   nivelAgua       = -1;  // -1 = ainda não lido

  // Debounce água
  int   aguaLeitura     = -1;
  int   aguaContador    = 0;

  // Bomba
  bool      bombaAtiva  = false;
  uint32_t  tempoBomba  = 0;

  // Alimentação
  bool    alimentando   = false;
  uint8_t ultimaH       = 0;
  uint8_t ultimaM       = 0;

  // Wi-Fi / Blynk
  bool online           = false;

  // Alertas (throttle)
  bool     alertaDep1   = false;
  bool     alertaDep2   = false;
  bool     alertaAgua   = false;
  uint32_t tsAlertaDep1 = 0;
  uint32_t tsAlertaDep2 = 0;
  uint32_t tsAlertaAgua = 0;
} est;

// Histórico de refeições
Refeicao historico[MAX_HISTORICO];
int totalRefeicoes = 0;

// Horários programados (-1 = não definido)
int8_t hor1H = -1, hor1M = -1;
int8_t hor2H = -1, hor2M = -1;

// =============================================================
// EEPROM — persistência de horários
// =============================================================
void eepromGuardar() {
  EEPROM.write(ADDR_H1H,  (uint8_t)(hor1H < 0 ? 0xFF : hor1H));
  EEPROM.write(ADDR_H1M,  (uint8_t)(hor1M < 0 ? 0xFF : hor1M));
  EEPROM.write(ADDR_H2H,  (uint8_t)(hor2H < 0 ? 0xFF : hor2H));
  EEPROM.write(ADDR_H2M,  (uint8_t)(hor2M < 0 ? 0xFF : hor2M));
  EEPROM.write(ADDR_MAGIC, EEPROM_MAGIC);
  EEPROM.commit();
  Serial.println(F("[EEPROM] Horários guardados."));
}

void eepromCarregar() {
  if (EEPROM.read(ADDR_MAGIC) != EEPROM_MAGIC) {
    Serial.println(F("[EEPROM] Sem dados válidos — defaults aplicados."));
    return;
  }
  auto rd = [](int addr) -> int8_t {
    uint8_t v = EEPROM.read(addr);
    return (v == 0xFF) ? -1 : (int8_t)v;
  };
  hor1H = rd(ADDR_H1H); hor1M = rd(ADDR_H1M);
  hor2H = rd(ADDR_H2H); hor2M = rd(ADDR_H2M);
  Serial.printf(F("[EEPROM] H1=%02d:%02d  H2=%02d:%02d\n"),
                hor1H, hor1M, hor2H, hor2M);
}

// =============================================================
// UTILITÁRIOS — Sensor Ultrassónico
// =============================================================

// Leitura única (retorna -1 se inválida)
static int lerDistRaw(uint8_t trig, uint8_t echo) {
  digitalWrite(trig, LOW);
  delayMicroseconds(2);
  digitalWrite(trig, HIGH);
  delayMicroseconds(10);
  digitalWrite(trig, LOW);
  long dur = pulseIn(echo, HIGH, 30000UL);
  if (dur == 0) return -1;
  int d = (int)(dur * 0.0170f);  // cm = us * velocidade_som / 2
  return (d > 0 && d < DIST_INVALIDA) ? d : -1;
}

// Média de leituras válidas
int lerDistFiltrada(uint8_t trig, uint8_t echo) {
  int soma = 0, validas = 0;
  for (int i = 0; i < LEITURAS_MEDIA; i++) {
    int d = lerDistRaw(trig, echo);
    if (d > 0) { soma += d; validas++; }
    delay(10);
  }
  return (validas > 0) ? (soma / validas) : -1;
}

// Converte distância em percentagem de enchimento
int distParaPerc(int dist) {
  if (dist < 0) return -1;
  return constrain(map(dist, DIST_CHEIO, DIST_VAZIO, 100, 0), 0, 100);
}

// =============================================================
// UTILITÁRIOS — Célula de Carga
// =============================================================
float lerPeso() {
  if (!balanca.is_ready()) return -1.0f;
  float v = balanca.get_units(5);
  return (v < 0.0f) ? 0.0f : v;
}

void realizarTara() {
  if (!balanca.is_ready()) return;
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString("TARA...", 10, 355, 2);
  balanca.tare(10);
  tft.fillRect(0, 352, 290, 22, TFT_BLACK);
  Serial.println(F("[Balanca] Tara realizada."));
}

// =============================================================
// UTILITÁRIOS — Sensor de Água (ADC + debounce)
// =============================================================

// Mediana de 5 amostras
static int adcMediana() {
  int buf[5];
  for (int i = 0; i < 5; i++) { buf[i] = analogRead(SENSOR_AGUA); delay(10); }
  // Bubble sort simples
  for (int i = 0; i < 4; i++)
    for (int j = i + 1; j < 5; j++)
      if (buf[i] > buf[j]) { int t = buf[i]; buf[i] = buf[j]; buf[j] = t; }
  return buf[2];
}

int lerNivelAgua() {
  int raw = adcMediana();
  static int ultimoRaw = -999;
  if (raw != ultimoRaw) {
    Serial.printf("[Agua] ADC mediana=%d\n", raw);
    ultimoRaw = raw;
  }
  if (raw < AGUA_LIM_VAZIO) return 0;  // Sem água
  if (raw < AGUA_LIM_BAIXO) return 1;  // Baixo
  if (raw < AGUA_LIM_OK)    return 2;  // OK
  return 3;                             // Cheio
}

// =============================================================
// BOMBA DE ÁGUA
// =============================================================
void gerarBomba(int nivel) {
  uint32_t agora = millis();

  // Ligar se nível ≤ 1 e bomba desligada
  if (nivel <= 1 && !est.bombaAtiva) {
    digitalWrite(BOMBA_PIN, HIGH);
    est.bombaAtiva  = true;
    est.tempoBomba  = agora;
    Serial.println(F("[Bomba] LIGADA — nível baixo"));
    if (Blynk.connected())
      Blynk.logEvent("bomba_ligada", "Bomba de agua ativada!");
  }

  // Desligar se nível OK ou timeout de segurança
  bool timeout = (agora - est.tempoBomba > BOMBA_TIMEOUT_MS);
  if (est.bombaAtiva && (nivel > 1 || timeout)) {
    digitalWrite(BOMBA_PIN, LOW);
    est.bombaAtiva = false;
    Serial.println(timeout ? F("[Bomba] DESLIGADA — timeout")
                           : F("[Bomba] DESLIGADA — nível OK"));
  }
}

// =============================================================
// DISPLAY
// =============================================================
static const char* labelAgua(int n) {
  switch (n) {
    case 0:  return "SEM AGUA";
    case 1:  return "BAIXO   ";
    case 2:  return "OK      ";
    default: return "CHEIO   ";
  }
}

static uint16_t corAgua(int n) {
  switch (n) {
    case 0:  return TFT_RED;
    case 1:  return TFT_ORANGE;
    case 2:  return TFT_GREEN;
    default: return TFT_CYAN;
  }
}

static uint16_t corNivel(int p) {
  if (p < NIVEL_CRITICO) return TFT_RED;
  if (p < NIVEL_BAIXO)   return TFT_YELLOW;
  return TFT_GREEN;
}

void atualizarHorariosEcra() {
  tft.fillRect(0, 335, 295, 22, TFT_BLACK);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  char h1[6], h2[6];
  snprintf(h1, sizeof(h1), hor1H >= 0 ? "%02d:%02d" : "--:--", hor1H, hor1M);
  snprintf(h2, sizeof(h2), hor2H >= 0 ? "%02d:%02d" : "--:--", hor2H, hor2M);
  char buf[32];
  snprintf(buf, sizeof(buf), "%s  |  %s", h1, h2);
  tft.drawString(buf, 10, 338, 2);
}

void desenharBase() {
  tft.fillScreen(TFT_BLACK);

  // Barra de topo
  tft.fillRect(0, 0, 480, 42, tft.color565(25, 25, 25));
  tft.setTextColor(TFT_CYAN, tft.color565(25, 25, 25));
  tft.setCursor(80, 10);
  tft.setTextFont(4);
  tft.print("SMART PET FEEDER");

  // Labels coluna esquerda
  const uint16_t cinza = tft.color565(130, 130, 130);
  tft.setTextColor(cinza, TFT_BLACK);
  tft.drawString("PESO TIGELA",  10,  50, 2);
  tft.drawString("NIVEL AGUA",   10, 115, 2);
  tft.drawString("ULT. REFEIC.", 10, 180, 2);
  tft.drawString("HORARIOS",     10, 320, 2);

  // Linhas divisórias
  const uint16_t divCor = tft.color565(50, 50, 50);
  tft.drawFastHLine(5, 105, 285, divCor);
  tft.drawFastHLine(5, 170, 285, divCor);
  tft.drawFastHLine(5, 230, 285, divCor);
  tft.drawFastHLine(5, 313, 285, divCor);

  // Depósitos (tanques visuais)
  const uint16_t bordaCor = tft.color565(80, 80, 80);
  tft.drawRoundRect(295, 48,  85, 272, 8, bordaCor);
  tft.drawRoundRect(390, 48,  85, 272, 8, bordaCor);
  tft.setTextColor(cinza, TFT_BLACK);
  tft.drawString("DEP1", 313, 53, 2);
  tft.drawString("DEP2", 408, 53, 2);

  // Marcas de escala (25 / 50 / 75 %)
  for (int p = 25; p <= 75; p += 25) {
    int y = map(p, 0, 100, 314, 70);
    tft.drawFastHLine(296, y, 8, bordaCor);
    tft.drawFastHLine(391, y, 8, bordaCor);
  }

  atualizarHorariosEcra();
}

void atualizarTanque(int xOuter, int w, int perc) {
  if (perc < 0) return;
  int xi  = xOuter + 5, wi = w - 10;
  int tY  = 70, bY = 315, alt = bY - tY;
  int barH = map(constrain(perc, 0, 100), 0, 100, 0, alt);
  uint16_t cor = corNivel(perc);

  tft.fillRect(xi, tY, wi, alt - barH, TFT_BLACK);          // vazio (cima)
  if (barH > 0)
    tft.fillRect(xi, bY - barH, wi, barH, cor);              // cheio (baixo)

  // Percentagem dentro da barra (se houver espaço)
  if (barH > 22) {
    tft.setTextColor(TFT_BLACK, cor);
    char buf[5];
    snprintf(buf, sizeof(buf), "%d%%", perc);
    int tw = tft.textWidth(buf, 1);
    tft.drawString(buf, xOuter + (w - tw) / 2, bY - barH + 5, 1);
  }
}

void atualizarDepositos(int p1, int p2) {
  atualizarTanque(295, 85, p1 < 0 ? 0 : p1);
  atualizarTanque(390, 85, p2 < 0 ? 0 : p2);
}

void atualizarPesoEcra(float peso) {
  tft.fillRect(10, 62, 280, 36, TFT_BLACK);
  char buf[16];
  snprintf(buf, sizeof(buf), peso < 0 ? "  ----  " : "%6.1f g", peso);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(buf, 10, 65, 4);
}

void atualizarAguaEcra(int nivel) {
  uint16_t cor = corAgua(nivel);
  tft.fillRect(10, 127, 280, 36, TFT_BLACK);
  tft.setTextColor(cor, TFT_BLACK);
  tft.drawString(labelAgua(nivel), 10, 130, 4);
  if (est.bombaAtiva) {
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString("BOMBA ON", 160, 130, 4);
  }
}

void atualizarUltimaRefeicao() {
  char buf[14];
  if (est.ultimaH == 0 && est.ultimaM == 0)
    snprintf(buf, sizeof(buf), "  --:--");
  else
    snprintf(buf, sizeof(buf), "  %02d:%02d h", est.ultimaH, est.ultimaM);
  tft.fillRect(10, 192, 280, 32, TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(buf, 10, 195, 4);
}

void atualizarWifiEcra(bool online) {
  uint16_t bg = online ? tft.color565(0, 70, 0)  : tft.color565(70, 0, 0);
  uint16_t fg = online ? TFT_GREEN               : TFT_RED;
  tft.fillRect(358, 10, 115, 22, bg);
  tft.setTextColor(fg, bg);
  tft.drawString(online ? " ONLINE " : " OFFLINE", 362, 14, 2);
}

// =============================================================
// HISTÓRICO DE REFEIÇÕES
// =============================================================
void registarRefeicao(float peso) {
  for (int i = MAX_HISTORICO - 1; i > 0; i--)
    historico[i] = historico[i - 1];
  historico[0] = { (uint8_t)hour(), (uint8_t)minute(), peso };
  if (totalRefeicoes < MAX_HISTORICO) totalRefeicoes++;
}

// =============================================================
// ALIMENTAÇÃO
// =============================================================
void darComida() {
  if (est.alimentando) return;
  est.alimentando = true;

  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.drawString("A DISPENSAR RACAO...", 10, 355, 2);

  motor->setSpeed(STEPPER_SPEED_RPM);
  motor->step(PORCAO_STEPS);

  // Desligar bobines para evitar aquecimento
  digitalWrite(MOTOR_IN1, LOW);
  digitalWrite(MOTOR_IN2, LOW);
  digitalWrite(MOTOR_IN3, LOW);
  digitalWrite(MOTOR_IN4, LOW);

  est.ultimaH = (uint8_t)hour();
  est.ultimaM = (uint8_t)minute();
  registarRefeicao(est.pesoAtual);
  atualizarUltimaRefeicao();

  tft.fillRect(0, 352, 290, 22, TFT_BLACK);
  est.alimentando = false;
  Serial.printf("[Motor] Porção dispensada às %02d:%02d | %.1fg na tigela\n",
                est.ultimaH, est.ultimaM, est.pesoAtual);
}

// =============================================================
// VERIFICAÇÃO DE HORÁRIOS
// =============================================================
void verificarHorarios() {
  if (hor1H < 0 && hor2H < 0) return;

  int h = hour(), m = minute(), s = second();
  // Só atua na primeira metade do minuto (s < 30)
  if (s >= 30) return;

  static int8_t ultimoH1 = -1, ultimoH2 = -1;

  if (hor1H >= 0 && h == hor1H && m == hor1M && ultimoH1 != m) {
    Serial.println(F("[Horario 1] Hora de dar comida!"));
    ultimoH1 = m;
    darComida();
  }
  if (hor2H >= 0 && h == hor2H && m == hor2M && ultimoH2 != m) {
    Serial.println(F("[Horario 2] Hora de dar comida!"));
    ultimoH2 = m;
    darComida();
  }
}

// =============================================================
// TAREFA PRINCIPAL — Leitura de sensores
// =============================================================
void tarefaSensores() {
  uint32_t agora = millis();

  // ── Depósito 1 (ração) ──────────────────────────────────────
  int d1 = lerDistFiltrada(TRIG_DEP1, ECHO_DEP1);
  int p1 = distParaPerc(d1);
  if (p1 >= 0 && p1 != est.dep1Perc) {
    est.dep1Perc = p1;
    atualizarDepositos(est.dep1Perc, est.dep2Perc);
    if (Blynk.connected()) Blynk.virtualWrite(VP_DEP1, p1);
  }
  if (p1 >= 0 && p1 < NIVEL_CRITICO && Blynk.connected()) {
    if (!est.alertaDep1 || agora - est.tsAlertaDep1 > INTERVALO_ALERTA_MS) {
      Blynk.logEvent("nivel_baixo", "Deposito 1: nivel de racao critico (<20%)!");
      est.alertaDep1    = true;
      est.tsAlertaDep1  = agora;
    }
  } else if (p1 >= NIVEL_CRITICO) est.alertaDep1 = false;

  // ── Depósito 2 ──────────────────────────────────────────────
  int d2 = lerDistFiltrada(TRIG_DEP2, ECHO_DEP2);
  int p2 = distParaPerc(d2);
  if (p2 >= 0 && p2 != est.dep2Perc) {
    est.dep2Perc = p2;
    atualizarDepositos(est.dep1Perc, est.dep2Perc);
    if (Blynk.connected()) Blynk.virtualWrite(VP_DEP2, p2);
  }
  if (p2 >= 0 && p2 < NIVEL_CRITICO && Blynk.connected()) {
    if (!est.alertaDep2 || agora - est.tsAlertaDep2 > INTERVALO_ALERTA_MS) {
      Blynk.logEvent("nivel_baixo2", "Deposito 2: nivel de racao critico (<20%)!");
      est.alertaDep2    = true;
      est.tsAlertaDep2  = agora;
    }
  } else if (p2 >= NIVEL_CRITICO) est.alertaDep2 = false;

  // ── Célula de carga ─────────────────────────────────────────
  float peso = lerPeso();
  if (peso >= 0.0f && fabsf(peso - est.pesoAtual) > 0.5f) {
    est.pesoAtual = peso;
    atualizarPesoEcra(est.pesoAtual);
    if (Blynk.connected()) Blynk.virtualWrite(VP_PESO, est.pesoAtual);
  }

  delay(50);  // Separação temporal — reduz interferência no ADC

  // ── Sensor de água (mediana + debounce) ────────────────────
  int novoNivel = lerNivelAgua();
  if (novoNivel == est.aguaLeitura) {
    est.aguaContador++;
  } else {
    est.aguaLeitura  = novoNivel;
    est.aguaContador = 0;
  }
  if (est.aguaContador >= AGUA_DEBOUNCE && novoNivel != est.nivelAgua) {
    est.nivelAgua = novoNivel;
    atualizarAguaEcra(est.nivelAgua);
    gerarBomba(est.nivelAgua);
    if (Blynk.connected()) Blynk.virtualWrite(VP_AGUA, est.nivelAgua);
  }

  // Timeout de segurança da bomba (mesmo sem mudança de nível)
  if (est.bombaAtiva && agora - est.tempoBomba > BOMBA_TIMEOUT_MS) {
    digitalWrite(BOMBA_PIN, LOW);
    est.bombaAtiva = false;
    atualizarAguaEcra(est.nivelAgua);
    Serial.println(F("[Bomba] Timeout de segurança — desligada forçosamente."));
  }

  // Alerta água vazia
  if (est.nivelAgua == 0 && Blynk.connected()) {
    if (!est.alertaAgua || agora - est.tsAlertaAgua > INTERVALO_ALERTA_MS) {
      Blynk.logEvent("sem_agua", "Bebedouro vazio!");
      est.alertaAgua   = true;
      est.tsAlertaAgua = agora;
    }
  } else if (est.nivelAgua > 0) est.alertaAgua = false;

  // ── Estado Wi-Fi ────────────────────────────────────────────
  bool agOra = Blynk.connected();
  Serial.printf("[Debug WiFi] Blynk connected = %d | est.online = %d\n", agOra, est.online);
  if (agOra != est.online) {
    est.online = agOra;
    Serial.printf("[Debug WiFi] A atualizar ecrã para: %s\n", est.online ? "ONLINE" : "OFFLINE");
    atualizarWifiEcra(est.online);
  }
}

// =============================================================
// BLYNK — Callbacks
// =============================================================
BLYNK_CONNECTED() {
  rtc.begin();
  Blynk.syncAll();
  Serial.println(F("[Blynk] Ligado ao servidor."));
}

BLYNK_DISCONNECTED() {
  Serial.println(F("[Blynk] Desligado do servidor."));
}

BLYNK_WRITE(V1) { if (param.asInt() == 1) darComida(); }
BLYNK_WRITE(V6) { if (param.asInt() == 1) realizarTara(); }

BLYNK_WRITE(V7) {
  TimeInputParam t(param);
  if (t.hasStartTime()) {
    hor1H = t.getStartHour();
    hor1M = t.getStartMinute();
    Serial.printf("[Horario 1] Definido: %02d:%02d\n", hor1H, hor1M);
    eepromGuardar();
    atualizarHorariosEcra();
  }
}

BLYNK_WRITE(V8) {
  TimeInputParam t(param);
  if (t.hasStartTime()) {
    hor2H = t.getStartHour();
    hor2M = t.getStartMinute();
    Serial.printf("[Horario 2] Definido: %02d:%02d\n", hor2H, hor2M);
    eepromGuardar();
    atualizarHorariosEcra();
  }
}

BLYNK_WRITE(V9) {
  bool ligar = (param.asInt() == 1);
  digitalWrite(BOMBA_PIN, ligar ? HIGH : LOW);
  est.bombaAtiva = ligar;
  if (ligar) est.tempoBomba = millis();
  Serial.printf("[Bomba] %s manualmente via Blynk.\n", ligar ? "LIGADA" : "DESLIGADA");
  atualizarAguaEcra(est.nivelAgua);
}

BLYNK_WRITE(V11) {
  if (param.asInt() != 1) return;
  Serial.println(F("[WiFi] A escaniar redes..."));
  int n = WiFi.scanNetworks();
  String lista = (n > 0) ? "" : "Nenhuma rede detetada";
  for (int i = 0; i < min(n, 5); i++) {
    if (i > 0) lista += ",";
    lista += WiFi.SSID(i);
  }
  Blynk.virtualWrite(VP_WIFI_REDES, lista);
  Serial.println("[WiFi] Redes: " + lista);
}

// =============================================================
// SETUP
// =============================================================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println(F("\n=== Smart Pet Feeder — Arranque ==="));

  // ── Display TFT (primeiro, para mostrar progresso no ecrã) ──
  tft.init();
  tft.setRotation(1);
  desenharBase();
  atualizarUltimaRefeicao();
  atualizarWifiEcra(false);
  atualizarHorariosEcra();

  // ── EEPROM ──────────────────────────────────────────────────
  EEPROM.begin(EEPROM_SIZE);
  eepromCarregar();

  motor = new Stepper(STEPPER_STEPS, MOTOR_IN1, MOTOR_IN2, MOTOR_IN3, MOTOR_IN4);
  motor->setSpeed(STEPPER_SPEED_RPM);
  // Garantir bobines desligadas ao arranque
  digitalWrite(MOTOR_IN1, LOW); digitalWrite(MOTOR_IN2, LOW);
  digitalWrite(MOTOR_IN3, LOW); digitalWrite(MOTOR_IN4, LOW);

  // ── Sensores ultrassónicos ──────────────────────────────────
  pinMode(TRIG_DEP1, OUTPUT); pinMode(ECHO_DEP1, INPUT);
  pinMode(TRIG_DEP2, OUTPUT); pinMode(ECHO_DEP2, INPUT);

  // ── Sensor de água + Bomba ──────────────────────────────────
  pinMode(SENSOR_AGUA, INPUT);      // GPIO34 — sem pull-up interno
  pinMode(BOMBA_PIN, OUTPUT);
  digitalWrite(BOMBA_PIN, LOW);     // Bomba sempre desligada no arranque
  analogReadResolution(12);

  // ── Célula de carga ─────────────────────────────────────────
  balanca.begin(HX711_DT, HX711_SCK);
  balanca.set_scale(FATOR_CALIBRACAO);
  if (TARA_NO_BOOT) {
    delay(500);
    if (balanca.is_ready()) {
      balanca.tare(10);
      Serial.println(F("[Balanca] Tara de arranque realizada."));
    } else {
      Serial.println(F("[Balanca] Nao encontrada — a saltar tara."));
    }
  }

  // ── Ligação Wi-Fi ───────────────────────────────────────────
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString("A conectar ao Wi-Fi...", 10, 355, 2);

  WiFi.persistent(false);
  WiFi.mode(WIFI_OFF);
  delay(300);
  WiFi.mode(WIFI_STA);
  delay(100);
  WiFi.begin(SSID, PASS);

  int tentativas = 0;
  while (WiFi.status() != WL_CONNECTED && tentativas < 30) {
    delay(500);
    Serial.printf("  Wi-Fi... tentativa %d/30\n", ++tentativas);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("  IP: " + WiFi.localIP().toString());
  } else {
    Serial.println(F("  Wi-Fi falhou — modo offline (horários da EEPROM ativos)."));
  }
  tft.fillRect(0, 352, 290, 22, TFT_BLACK);

  // ── Blynk ───────────────────────────────────────────────────
  Blynk.config(AUTH);
  Blynk.connect(3000);  // timeout de 3s para não bloquear

  // ── Temporizadores ──────────────────────────────────────────
  timer.setInterval(INTERVALO_SENSOR_MS,  tarefaSensores);
  timer.setInterval(INTERVALO_HORARIO_MS, verificarHorarios);

  Serial.println(F("=== Arranque concluído ===\n"));
}

// =============================================================
// LOOP
// =============================================================
void loop() {
  Blynk.run();
  timer.run();
}
