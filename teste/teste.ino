// =============================================================
// TESTE — Sensor Ultrassónico + Ecrã + Motor Stepper + WiFi/Blynk
// =============================================================

#define BLYNK_TEMPLATE_ID   "TMPL5mbVhAiea"
#define BLYNK_TEMPLATE_NAME "LED ESP32"
#define BLYNK_PRINT         Serial

#include <SPI.h>
#include <TFT_eSPI.h>
#include <Stepper.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <BlynkSimpleEsp32.h>
#include <WidgetRTC.h>

// ── Credenciais ───────────────────────────────────────────────
const char AUTH[] = "sOOvSR9kmZxS_CMp5pvbqQHDbGvJczP1";
const char SSID[] = "NOS-3CF6";
const char PASS[] = "G4HWAXEU";

// ── Pinos ─────────────────────────────────────────────────────
#define TRIG_PIN     33
#define ECHO_PIN     26

#define MOTOR_IN1    13
#define MOTOR_IN2    14
#define MOTOR_IN3    12
#define MOTOR_IN4    27

// ── Configuração ───────────────────────────────────────────────
#define STEPPER_STEPS     2048
#define STEPPER_SPEED_RPM 15
#define PORCAO_STEPS      1024

#define DIST_CHEIO        5
#define DIST_VAZIO        40
#define DIST_INVALIDA     100

// Pinos Virtuais Blynk
#define VP_ALIMENTAR  V1
#define VP_NIVEL      V2

// ── Objetos globais ────────────────────────────────────────────
TFT_eSPI tft;
Stepper* motor = nullptr;
WidgetRTC rtc;
BlynkTimer timer;

// ── Variáveis ──────────────────────────────────────────────────
bool dispensando = false;
int nivelAtual   = -1;

// =============================================================
// Sensor Ultrassónico
// =============================================================
int lerDistancia() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long dur = pulseIn(ECHO_PIN, HIGH, 30000UL);
  if (dur == 0) return -1;
  int d = (int)(dur * 0.0170f);
  return (d > 0 && d < DIST_INVALIDA) ? d : -1;
}

int distParaPerc(int dist) {
  if (dist < 0) return -1;
  return constrain(map(dist, DIST_CHEIO, DIST_VAZIO, 100, 0), 0, 100);
}

// =============================================================
// Ecrã
// =============================================================
void desenharBase() {
  tft.fillScreen(TFT_BLACK);
  tft.fillRect(0, 0, 480, 42, tft.color565(25, 25, 25));
  tft.setTextColor(TFT_CYAN, tft.color565(25, 25, 25));
  tft.setCursor(90, 10);
  tft.setTextFont(4);
  tft.print("SMART PET FEEDER");

  const uint16_t cinza = tft.color565(130, 130, 130);
  tft.setTextColor(cinza, TFT_BLACK);
  tft.drawString("NIVEL RACAO", 10, 60, 2);
  tft.drawString("DISTANCIA",   10, 130, 2);
  tft.drawString("MOTOR",       10, 200, 2);

  tft.drawRoundRect(300, 50, 160, 280, 8, tft.color565(80, 80, 80));
  tft.drawString("DEPOSITO", 335, 55, 2);

  for (int p = 25; p <= 75; p += 25) {
    int y = map(p, 0, 100, 320, 80);
    tft.drawFastHLine(301, y, 10, tft.color565(80, 80, 80));
  }
}

void atualizarWifiEcra(bool online) {
  uint16_t bg = online ? tft.color565(0, 70, 0) : tft.color565(70, 0, 0);
  uint16_t fg = online ? TFT_GREEN : TFT_RED;
  tft.fillRect(358, 10, 115, 22, bg);
  tft.setTextColor(fg, bg);
  tft.drawString(online ? " ONLINE " : " OFFLINE", 362, 14, 2);
}

void atualizarDeposito(int perc) {
  if (perc < 0) return;
  int xi = 305, wi = 150;
  int tY = 80, bY = 320, alt = bY - tY;
  int barH = map(constrain(perc, 0, 100), 0, 100, 0, alt);

  uint16_t cor = (perc < 20) ? TFT_RED :
                 (perc < 60) ? TFT_YELLOW : TFT_GREEN;

  tft.fillRect(xi, tY, wi, alt - barH, TFT_BLACK);
  if (barH > 0)
    tft.fillRect(xi, bY - barH, wi, barH, cor);

  if (barH > 22) {
    tft.setTextColor(TFT_BLACK, cor);
    char buf[6];
    snprintf(buf, sizeof(buf), "%d%%", perc);
    tft.drawString(buf, 355, bY - barH + 5, 2);
  }
}

void atualizarNivel(int perc, int dist) {
  tft.fillRect(10, 75, 280, 40, TFT_BLACK);
  char buf[16];
  if (perc < 0) snprintf(buf, sizeof(buf), "-- %%");
  else          snprintf(buf, sizeof(buf), "%d %%", perc);

  uint16_t cor = (perc < 0)  ? TFT_WHITE :
                 (perc < 20) ? TFT_RED :
                 (perc < 60) ? TFT_YELLOW : TFT_GREEN;
  tft.setTextColor(cor, TFT_BLACK);
  tft.drawString(buf, 10, 80, 4);

  tft.fillRect(10, 145, 280, 40, TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  if (dist < 0) tft.drawString("---- cm", 10, 150, 4);
  else {
    char d[12];
    snprintf(d, sizeof(d), "%d cm", dist);
    tft.drawString(d, 10, 150, 4);
  }
}

void atualizarStatusMotor(const char* msg, uint16_t cor) {
  tft.fillRect(10, 215, 280, 40, TFT_BLACK);
  tft.setTextColor(cor, TFT_BLACK);
  tft.drawString(msg, 10, 220, 4);
}

// =============================================================
// Motor — Dispensar
// =============================================================
void dispensarPorcao() {
  if (dispensando || motor == nullptr) return;
  dispensando = true;

  atualizarStatusMotor("A DISPENSAR...", TFT_ORANGE);
  Serial.println("[Motor] A dispensar porcao...");

  motor->setSpeed(STEPPER_SPEED_RPM);
  motor->step(PORCAO_STEPS);

  digitalWrite(MOTOR_IN1, LOW);
  digitalWrite(MOTOR_IN2, LOW);
  digitalWrite(MOTOR_IN3, LOW);
  digitalWrite(MOTOR_IN4, LOW);

  Serial.println("[Motor] Porcao dispensada!");
  atualizarStatusMotor("DISPENSADO!", TFT_GREEN);

  if (Blynk.connected())
    Blynk.logEvent("alimentacao", "Porcao dispensada!");

  delay(2000);
  atualizarStatusMotor("PRONTO", TFT_WHITE);
  dispensando = false;
}

// =============================================================
// Tarefa periódica — Leitura do sensor
// =============================================================
void tarefaSensor() {
  int dist = lerDistancia();
  int perc = distParaPerc(dist);

  if (perc != nivelAtual) {
    nivelAtual = perc;
    atualizarNivel(perc, dist);
    atualizarDeposito(perc);
    Serial.printf("[Sensor] Dist=%dcm | Nivel=%d%%\n", dist, perc);

    if (Blynk.connected())
      Blynk.virtualWrite(VP_NIVEL, perc);

    // Alerta nível crítico
    if (perc >= 0 && perc < 20 && Blynk.connected())
      Blynk.logEvent("nivel_baixo", "Nivel de racao critico (<20%)!");
  }

  // Atualiza estado WiFi no ecrã
  static bool lastOnline = false;
  bool agora = Blynk.connected();
  if (agora != lastOnline) {
    lastOnline = agora;
    atualizarWifiEcra(agora);
  }
}

// =============================================================
// Blynk — Callbacks
// =============================================================
BLYNK_CONNECTED() {
  rtc.begin();
  Blynk.syncAll();
  Serial.println("[Blynk] Ligado ao servidor.");
}

BLYNK_WRITE(V1) {
  if (param.asInt() == 1) dispensarPorcao();
}

// =============================================================
// SETUP
// =============================================================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== TESTE Smart Pet Feeder ===");

  // ── Ecrã (PRIMEIRO!) ─────────────────────────────────────────
  tft.init();
  tft.setRotation(1);
  desenharBase();
  atualizarWifiEcra(false);
  atualizarStatusMotor("A ARRANCAR...", TFT_YELLOW);
  Serial.println("[TFT] Ecra inicializado.");

  // ── Sensor ultrassónico ──────────────────────────────────────
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  // ── Motor (pointer para evitar crash no arranque) ────────────
  motor = new Stepper(STEPPER_STEPS, MOTOR_IN1, MOTOR_IN2, MOTOR_IN3, MOTOR_IN4);
  motor->setSpeed(STEPPER_SPEED_RPM);
  digitalWrite(MOTOR_IN1, LOW);
  digitalWrite(MOTOR_IN2, LOW);
  digitalWrite(MOTOR_IN3, LOW);
  digitalWrite(MOTOR_IN4, LOW);
  Serial.println("[Motor] Stepper pronto.");

  // ── WiFi ─────────────────────────────────────────────────────
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString("A conectar ao WiFi...", 10, 420, 2);

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASS);

  int tentativas = 0;
  while (WiFi.status() != WL_CONNECTED && tentativas < 20) {
    delay(500);
    Serial.printf("  WiFi... %d/20\n", ++tentativas);
  }

  tft.fillRect(0, 416, 480, 24, TFT_BLACK);

  if (WiFi.status() == WL_CONNECTED)
    Serial.println("  IP: " + WiFi.localIP().toString());
  else
    Serial.println("  WiFi falhou — modo offline.");

  // ── Blynk ────────────────────────────────────────────────────
  Blynk.config(AUTH);
  Blynk.connect(3000);

  // ── Timer ────────────────────────────────────────────────────
  timer.setInterval(2000L, tarefaSensor);

  atualizarStatusMotor("PRONTO", TFT_WHITE);
  Serial.println("=== Arranque concluido ===\n");
}

// =============================================================
// LOOP
// =============================================================
void loop() {
  Blynk.run();
  timer.run();
}
