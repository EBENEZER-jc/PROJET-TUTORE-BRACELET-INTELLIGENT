/**
 * =============================================================================
 *  UPAC – EBEN-EZER  |  Version Professionnelle
 *  Unité de Prévention et d'Accompagnement Clinique
 *  Plateforme IoT de télésurveillance médicale – ESP32
 * =============================================================================
 *
 *  Capteurs :
 *    DS18B20  → Température corporelle (réelle)
 *    MQ135    → Qualité de l'air ambiant
 *    MAX30102 → Fréquence cardiaque & SpO2 (simulés, remplaçables)
 *
 *  Bibliothèques : WiFi, WiFiClientSecure, Wire, LiquidCrystal_I2C,
 *                   OneWire, DallasTemperature, BlynkSimpleEsp32_SSL
 *
 *  @version 2.0.0-PRO
 *  @author  UPAC EBEN-EZER
 */

// =============================================================================
//  IDENTITÉ DU DISPOSITIF
// =============================================================================

#define DEVICE_NAME         "UPAC-EBEN-EZER"
#define DEVICE_VERSION      "v2.0 PRO"
#define INSTITUTION         "UPAC EBEN-EZER"
#define CLINICAL_UNIT       "Unite Surveillance Clinique"

// =============================================================================
//  CONFIGURATION RÉSEAU & SERVICES
// =============================================================================

#define WIFI_SSID           "La_Fibre_dOrange_2.4G_3C07"
#define WIFI_PASSWORD       "CXSC9NPPT956UKRZ2A"
#define BLYNK_TEMPLATE_ID   "TMPL2146Ewb4q"
#define BLYNK_TEMPLATE_NAME "PROJET TUTORE"
#define BLYNK_AUTH_TOKEN    "neuy2mDF5ZD8hD2g2HPFE99nM0ayQJk0"
#define BLYNK_SERVER        "blynk.cloud"
#define BLYNK_PORT          443
#define BLYNK_EVENT_ALERTE  "alerte_medicale"

#define SMTP_HOST           "smtp.gmail.com"
#define SMTP_PORT           465
#define SMTP_EMAIL          "mambeeben@gmail.com"
#define SMTP_APP_PASSWORD   "oafp bjxc jxoi nbxp"
#define ALERT_EMAIL_TO      "mambeeben@gmail.com"

// =============================================================================
//  MATÉRIEL – BROCHAGE ESP32 DevKit V1
// =============================================================================

#define PIN_LCD_SDA         21
#define PIN_LCD_SCL         22
#define LCD_I2C_ADDR        0x27
#define LCD_COLS            16
#define LCD_ROWS            2
#define PIN_DS18B20         4
#define PIN_MQ135           34

// =============================================================================
//  SEUILS CLINIQUES & ENVIRONNEMENTAUX
// =============================================================================

#define MQ135_THRESHOLD_EXCELLENT  1000
#define MQ135_THRESHOLD_BON        1400
#define MQ135_THRESHOLD_MOYEN      1800
#define MQ135_THRESHOLD_MAUVAIS    2200

#define THRESH_TEMP_FEVER_C       38.0f   // Hyperthermie
#define THRESH_FC_MIN             55      // Bradycardie
#define THRESH_FC_MAX             110     // Tachycardie
#define THRESH_SPO2_MIN           92      // Hypoxémie

// =============================================================================
//  TEMPORISATIONS (ms)
// =============================================================================

#define INTERVAL_SENSOR_MS        2000UL
#define INTERVAL_LCD_PAGE_MS      5000UL
#define INTERVAL_CRISIS_MS        60000UL
#define DURATION_CRISIS_MS        10000UL
#define INTERVAL_BOOT_MSG_MS      1500UL
#define TIMEOUT_WIFI_MS           30000UL
#define INTERVAL_DEBUG_MS         10000UL

// =============================================================================
//  OPTIMISATION FIRMWARE
// =============================================================================

#define BLYNK_MSG_LIMIT           8
#define BLYNK_MAX_READBYTES       128
#define BLYNK_MAX_SENDBYTES       64
#define BLYNK_MAX_TIMERS          4

// =============================================================================
//  INCLUDES
// =============================================================================

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <time.h>
#include <LiquidCrystal_I2C.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <BlynkSimpleEsp32_SSL.h>

// =============================================================================
//  ÉNUMÉRATIONS & STRUCTURES
// =============================================================================

enum AirQualityLevel : uint8_t {
  AIR_OPTIMAL = 0,
  AIR_CORRECT,
  AIR_MODERE,
  AIR_DEGRADE,
  AIR_CRITIQUE
};

enum SystemBootPhase : uint8_t {
  PHASE_SPLASH,
  PHASE_INIT,
  PHASE_CONNECT,
  PHASE_READY,
  PHASE_MONITORING
};

struct VitalSigns {
  float          bodyTempC;
  int            heartRateBpm;
  int            spo2Percent;
  int            airSensorRaw;
  AirQualityLevel airQuality;
  bool           clinicalAlert;
};

struct AlertFlags {
  bool hyperthermia;
  bool bradycardia;
  bool tachycardia;
  bool hypoxemia;
  bool airCritical;
};

// =============================================================================
//  OBJETS GLOBALS
// =============================================================================

LiquidCrystal_I2C  lcd(LCD_I2C_ADDR, LCD_COLS, LCD_ROWS);
OneWire            oneWireBus(PIN_DS18B20);
DallasTemperature  tempSensor(&oneWireBus);

VitalSigns   vitals     = {NAN, 0, 0, 0, AIR_CORRECT, false};
AlertFlags   alertFlags = {false, false, false, false, false};

SystemBootPhase bootPhase       = PHASE_SPLASH;
uint8_t         bootStep        = 0;
uint8_t         lcdPageIndex    = 0;
unsigned long   bootStepMs      = 0;
unsigned long   connectStartMs  = 0;
unsigned long   lastSensorMs    = 0;
unsigned long   lastLcdPageMs   = 0;
unsigned long   lastCrisisMs    = 0;
unsigned long   crisisEndMs     = 0;
unsigned long   lastDebugMs     = 0;
bool            crisisSimActive = false;
bool            blynkNotifSent  = false;
bool            emailSent       = false;
bool            wifiStarted     = false;
bool            blynkConfigured = false;
bool            ntpSynced       = false;

// =============================================================================
//  PROTOTYPES
// =============================================================================

void     setup();
void     loop();
void     connectWiFi();
bool     connectBlynk();
void     syncTimeNTP();
void     initBlynkConfig();
void     logSystemStatus();
float    readBodyTemperature();
void     readAirQuality();
void     simulateVitalSigns();
AirQualityLevel classifyAirQuality();
bool     evaluateClinicalAlerts();
void     dispatchAlerts();
void     updateCloudDashboard();
void     pushClinicalNotification();
void     sendClinicalEmail();
bool     sendSmtpEmail(const char* subject, const char* body);
void     buildClinicalEmailBody(char* buf, size_t len);
const char* airQualityLabel(AirQualityLevel level);
const char* patientStatusLabel();
void     getTimestamp(char* buf, size_t len);
void     renderStartupScreen();
void     renderBootMessage(const char* line1, const char* line2);
void     renderReadyScreen();
void     renderMonitoringScreen();
void     renderClinicalAlertScreen();
void     padLine16(char* line);

// =============================================================================
//  SETUP
// =============================================================================

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println(F("============================================"));
  Serial.println(F("  UPAC EBEN-EZER - Plateforme Clinique PRO"));
  Serial.print(F("  Version : ")); Serial.println(DEVICE_VERSION);
  Serial.println(F("============================================"));

  analogReadResolution(12);
  pinMode(PIN_MQ135, INPUT);

  Wire.begin(PIN_LCD_SDA, PIN_LCD_SCL);
  lcd.init();
  lcd.backlight();

  tempSensor.begin();
  tempSensor.setResolution(12);

  randomSeed(analogRead(PIN_MQ135) + millis());
  lastCrisisMs = millis();

  WiFi.mode(WIFI_STA);

  renderStartupScreen();
  bootPhase = PHASE_INIT;
  bootStepMs = millis();
  bootStep   = 0;
}

// =============================================================================
//  BOUCLE PRINCIPALE
// =============================================================================

void loop() {
  if (bootPhase != PHASE_MONITORING) {
    switch (bootPhase) {

      case PHASE_INIT: {
        static const char* L1[] = {
          "Initialisation",
          "Connexion WiFi",
          "Liaison Cloud"
        };
        static const char* L2[] = {
          "Capteurs...",
          "Reseau local",
          "Plateforme IoT"
        };
        if (bootStep == 0) {
          renderBootMessage(L1[0], L2[0]);
          bootStepMs = millis();
          bootStep = 1;
        } else if (bootStep <= 3) {
          if (millis() - bootStepMs >= INTERVAL_BOOT_MSG_MS) {
            if (bootStep < 3) renderBootMessage(L1[bootStep], L2[bootStep]);
            bootStepMs = millis();
            bootStep++;
          }
          connectWiFi();
          if (WiFi.status() == WL_CONNECTED) { connectBlynk(); Blynk.run(); }
        }
        if (bootStep > 3) {
          bootPhase = PHASE_CONNECT;
          connectStartMs = millis();
        }
        break;
      }

      case PHASE_CONNECT: {
        connectWiFi();
        bool wifiOk  = (WiFi.status() == WL_CONNECTED);
        bool blynkOk = false;
        if (wifiOk) { blynkOk = connectBlynk(); Blynk.run(); }
        if ((wifiOk && blynkOk) || (millis() - connectStartMs > TIMEOUT_WIFI_MS)) {
          bootPhase  = PHASE_READY;
          bootStepMs = millis();
        }
        break;
      }

      case PHASE_READY: {
        renderReadyScreen();
        if (millis() - bootStepMs >= INTERVAL_BOOT_MSG_MS) {
          bootPhase       = PHASE_MONITORING;
          lastSensorMs    = millis();
          lastLcdPageMs   = millis();
          Serial.println(F("[Systeme] Surveillance clinique active"));
          logSystemStatus();
        }
        break;
      }

      default:
        break;
    }
    return;
  }

  Blynk.run();
  logSystemStatus();

  unsigned long now = millis();

  if (now - lastSensorMs >= INTERVAL_SENSOR_MS) {
    lastSensorMs = now;

    float t = readBodyTemperature();
    if (!isnan(t)) vitals.bodyTempC = t;

    readAirQuality();
    simulateVitalSigns();

    bool wasAlert = vitals.clinicalAlert;
    evaluateClinicalAlerts();

    if (vitals.clinicalAlert && !wasAlert) blynkNotifSent = false;
    if (!vitals.clinicalAlert) blynkNotifSent = false;

    if (vitals.clinicalAlert) dispatchAlerts();

    updateCloudDashboard();
    renderMonitoringScreen();
  }

  if (now - lastLcdPageMs >= INTERVAL_LCD_PAGE_MS) {
    lastLcdPageMs = now;
    lcdPageIndex = (lcdPageIndex + 1) % 2;
    renderMonitoringScreen();
  }
}

// =============================================================================
//  CONNECTIVITÉ
// =============================================================================

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  if (!wifiStarted) {
    Serial.print(F("[Reseau] Association : "));
    Serial.println(WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    wifiStarted = true;
  }
}

void syncTimeNTP() {
  if (ntpSynced || WiFi.status() != WL_CONNECTED) return;

  Serial.println(F("[Horloge] Synchronisation NTP..."));
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");

  struct tm ti;
  for (int i = 0; i < 20; i++) {
    if (getLocalTime(&ti)) {
      ntpSynced = true;
      Serial.println(F("[Horloge] Reference temporelle OK"));
      return;
    }
    delay(500);
  }
  Serial.println(F("[Horloge] NTP indisponible"));
}

void initBlynkConfig() {
  if (blynkConfigured || WiFi.status() != WL_CONNECTED) return;
  syncTimeNTP();
  Blynk.config(BLYNK_AUTH_TOKEN, BLYNK_SERVER, BLYNK_PORT);
  blynkConfigured = true;
  Serial.println(F("[Cloud] Blynk IoT configure (SSL/TLS)"));
}

bool connectBlynk() {
  if (WiFi.status() != WL_CONNECTED) return false;
  initBlynkConfig();
  Blynk.run();
  return Blynk.connected();
}

void logSystemStatus() {
  if (millis() - lastDebugMs < INTERVAL_DEBUG_MS) return;
  lastDebugMs = millis();

  Serial.print(F("[Statut] WiFi  : "));
  Serial.println(WiFi.status() == WL_CONNECTED ? F("CONNECTE") : F("HORS LIGNE"));
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print(F("[Statut] IP    : "));
    Serial.println(WiFi.localIP());
  }
  Serial.print(F("[Statut] Blynk : "));
  Serial.println(Blynk.connected() ? F("EN LIGNE") : F("DECONNECTE"));
  Serial.print(F("[Statut] Patient : "));
  Serial.println(patientStatusLabel());
}

// =============================================================================
//  ACQUISITION CAPTEURS
// =============================================================================

float readBodyTemperature() {
  tempSensor.requestTemperatures();
  float t = tempSensor.getTempCByIndex(0);
  if (t == DEVICE_DISCONNECTED_C) return NAN;
  if (t < 28.0f || t > 45.0f) return NAN;
  return t;
}

void readAirQuality() {
  long sum = 0;
  for (int i = 0; i < 5; i++) sum += analogRead(PIN_MQ135);
  vitals.airSensorRaw = (int)(sum / 5);
  vitals.airQuality   = classifyAirQuality();
}

AirQualityLevel classifyAirQuality() {
  int r = vitals.airSensorRaw;
  if (r < MQ135_THRESHOLD_EXCELLENT) return AIR_OPTIMAL;
  if (r < MQ135_THRESHOLD_BON)       return AIR_CORRECT;
  if (r < MQ135_THRESHOLD_MOYEN)     return AIR_MODERE;
  if (r < MQ135_THRESHOLD_MAUVAIS)   return AIR_DEGRADE;
  return AIR_CRITIQUE;
}

void simulateVitalSigns() {
  unsigned long now = millis();

  if (!crisisSimActive && (now - lastCrisisMs >= INTERVAL_CRISIS_MS)) {
    crisisSimActive = true;
    crisisEndMs     = now + DURATION_CRISIS_MS;
    lastCrisisMs    = now;
  }
  if (crisisSimActive && now >= crisisEndMs) crisisSimActive = false;

  if (crisisSimActive) {
    vitals.heartRateBpm = random(120, 146);
    vitals.spo2Percent  = random(84, 91);
  } else {
    vitals.heartRateBpm = random(68, 83);
    vitals.spo2Percent  = random(96, 100);
  }
}

const char* airQualityLabel(AirQualityLevel level) {
  switch (level) {
    case AIR_OPTIMAL:  return "Normal";
    case AIR_CORRECT:  return "Correct";
    case AIR_MODERE:   return "Modere";
    case AIR_DEGRADE:  return "Degrade";
    default:           return "Critique";
  }
}

const char* patientStatusLabel() {
  return vitals.clinicalAlert ? "INSTABLE" : "STABLE";
}

// =============================================================================
//  MOTEUR D'ALERTES CLINIQUES
// =============================================================================

bool evaluateClinicalAlerts() {
  alertFlags.hyperthermia = (!isnan(vitals.bodyTempC) && vitals.bodyTempC > THRESH_TEMP_FEVER_C);
  alertFlags.bradycardia  = (vitals.heartRateBpm < THRESH_FC_MIN);
  alertFlags.tachycardia  = (vitals.heartRateBpm > THRESH_FC_MAX);
  alertFlags.hypoxemia    = (vitals.spo2Percent < THRESH_SPO2_MIN);
  alertFlags.airCritical  = (vitals.airQuality == AIR_CRITIQUE);

  bool trigger = alertFlags.hyperthermia || alertFlags.bradycardia ||
                 alertFlags.tachycardia   || alertFlags.hypoxemia   ||
                 alertFlags.airCritical;

  vitals.clinicalAlert = trigger;
  if (!trigger) emailSent = false;

  return trigger;
}

void dispatchAlerts() {
  sendClinicalEmail();
  if (!blynkNotifSent) {
    pushClinicalNotification();
    blynkNotifSent = true;
  }
}

// =============================================================================
//  AFFICHAGE LCD 16×2
// =============================================================================

void padLine16(char* line) {
  size_t n = strlen(line);
  while (n < LCD_COLS) line[n++] = ' ';
  line[LCD_COLS] = '\0';
}

void renderStartupScreen() {
  lcd.clear();
  lcd.setCursor(1, 0);
  lcd.print("UPAC EBEN-EZER");
  lcd.setCursor(3, 1);
  lcd.print(DEVICE_VERSION);
  delay(3000);
}

void renderBootMessage(const char* line1, const char* line2) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(line1);
  lcd.setCursor(0, 1);
  lcd.print(line2);
}

void renderReadyScreen() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Poste actif");
  lcd.setCursor(0, 1);
  lcd.print("Surveillance ON");
}

void renderMonitoringScreen() {
  char L1[17], L2[17];

  if (vitals.clinicalAlert) {
    renderClinicalAlertScreen();
    return;
  }

  lcd.clear();

  if (lcdPageIndex == 0) {
    if (isnan(vitals.bodyTempC))
      snprintf(L1, sizeof(L1), "T corp: ---");
    else
      snprintf(L1, sizeof(L1), "T corp: %.1f C", vitals.bodyTempC);
    snprintf(L2, sizeof(L2), "Air: %s", airQualityLabel(vitals.airQuality));
  } else {
    snprintf(L1, sizeof(L1), "FC: %d bpm", vitals.heartRateBpm);
    snprintf(L2, sizeof(L2), "SpO2: %d %%", vitals.spo2Percent);
  }

  padLine16(L1);
  padLine16(L2);
  lcd.setCursor(0, 0); lcd.print(L1);
  lcd.setCursor(0, 1); lcd.print(L2);
}

void renderClinicalAlertScreen() {
  char L1[17], L2[17];
  lcd.clear();

  if (lcdPageIndex == 0) {
    snprintf(L1, sizeof(L1), "! URGENCE !");
    if (isnan(vitals.bodyTempC))
      snprintf(L2, sizeof(L2), "T: --- CRIT");
    else
      snprintf(L2, sizeof(L2), "T: %.1fC CRIT", vitals.bodyTempC);
  } else {
    snprintf(L1, sizeof(L1), "FC: %d CRIT", vitals.heartRateBpm);
    snprintf(L2, sizeof(L2), "SpO2: %d CRIT", vitals.spo2Percent);
  }

  padLine16(L1);
  padLine16(L2);
  lcd.setCursor(0, 0); lcd.print(L1);
  lcd.setCursor(0, 1); lcd.print(L2);
}

// =============================================================================
//  PLATEFORME CLOUD BLYNK
// =============================================================================

void updateCloudDashboard() {
  if (!Blynk.connected()) return;

  Blynk.virtualWrite(V0, isnan(vitals.bodyTempC) ? 0.0f : vitals.bodyTempC);
  Blynk.virtualWrite(V1, vitals.heartRateBpm);
  Blynk.virtualWrite(V2, vitals.spo2Percent);
  Blynk.virtualWrite(V3, vitals.airSensorRaw);
  Blynk.virtualWrite(V4, vitals.clinicalAlert ? "CRITIQUE" : "STABLE");
}

void pushClinicalNotification() {
  if (!Blynk.connected()) return;

  char msg[160];
  snprintf(msg, sizeof(msg),
           "ALERTE CLINIQUE | T:%.1fC FC:%d SpO2:%d%% Air:%s | Patient INSTABLE",
           isnan(vitals.bodyTempC) ? 0.0f : vitals.bodyTempC,
           vitals.heartRateBpm,
           vitals.spo2Percent,
           airQualityLabel(vitals.airQuality));

  Blynk.logEvent(BLYNK_EVENT_ALERTE, msg);
}

// =============================================================================
//  COURRIEL CLINIQUE (SMTP Gmail)
// =============================================================================

void getTimestamp(char* buf, size_t len) {
  struct tm ti;
  if (getLocalTime(&ti)) {
    strftime(buf, len, "%d/%m/%Y %H:%M:%S", &ti);
  } else {
    snprintf(buf, len, "Non synchronise");
  }
}

void buildClinicalEmailBody(char* buf, size_t len) {
  char ts[32];
  getTimestamp(ts, sizeof(ts));

  char causes[128] = "";
  if (alertFlags.hyperthermia) strcat(causes, "- Hyperthermie (>38 C)\r\n");
  if (alertFlags.bradycardia)  strcat(causes, "- Bradycardie (<55 bpm)\r\n");
  if (alertFlags.tachycardia)  strcat(causes, "- Tachycardie (>110 bpm)\r\n");
  if (alertFlags.hypoxemia)    strcat(causes, "- Hypoxemie (SpO2 <92%)\r\n");
  if (alertFlags.airCritical)  strcat(causes, "- Air ambiant critique\r\n");
  if (causes[0] == '\0')       strcpy(causes, "- Parametre hors norme\r\n");

  snprintf(buf, len,
    "========================================\r\n"
    " ALERTE CLINIQUE - %s\r\n"
    " %s\r\n"
    "========================================\r\n"
    "Priorite       : URGENTE\r\n"
    "Statut patient : INSTABLE\r\n"
    "Horodatage     : %s\r\n"
    "Dispositif     : %s %s\r\n"
    "\r\n"
    "--- PARAMETRES VITAUX ---\r\n"
    "Temperature corporelle : %.1f °C\r\n"
    "Frequence cardiaque    : %d bpm\r\n"
    "Saturation SpO2        : %d %%\r\n"
    "Qualite air ambiant    : %s\r\n"
    "\r\n"
    "--- ANOMALIES DETECTEES ---\r\n"
    "%s"
    "\r\n"
    "ACTION REQUISE : Intervention medicale immediate.\r\n"
    "Ce message a ete genere automatiquement par la\r\n"
    "plateforme de télésurveillance UPAC EBEN-EZER.\r\n",
    INSTITUTION,
    CLINICAL_UNIT,
    ts,
    DEVICE_NAME,
    DEVICE_VERSION,
    isnan(vitals.bodyTempC) ? 0.0f : vitals.bodyTempC,
    vitals.heartRateBpm,
    vitals.spo2Percent,
    airQualityLabel(vitals.airQuality),
    causes);
}

void sendClinicalEmail() {
  if (emailSent) return;

  char body[768];
  buildClinicalEmailBody(body, sizeof(body));

  if (sendSmtpEmail("UPAC | ALERTE CLINIQUE - Patient Instable", body)) {
    emailSent = true;
    Serial.println(F("[Alerte] Notification clinique transmise"));
  } else {
    Serial.println(F("[Alerte] Echec transmission email"));
  }
}

static const char B64T[] PROGMEM =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void base64Encode(const char* in, char* out, size_t outSize) {
  size_t len = strlen(in), j = 0;
  for (size_t i = 0; i < len; i += 3) {
    uint32_t n = (uint32_t)(uint8_t)in[i] << 16;
    if (i + 1 < len) n |= (uint32_t)(uint8_t)in[i + 1] << 8;
    if (i + 2 < len) n |= (uint32_t)(uint8_t)in[i + 2];
    if (j + 4 >= outSize) break;
    out[j++] = pgm_read_byte(&B64T[(n >> 18) & 63]);
    out[j++] = pgm_read_byte(&B64T[(n >> 12) & 63]);
    out[j++] = (i + 1 < len) ? pgm_read_byte(&B64T[(n >> 6) & 63]) : '=';
    out[j++] = (i + 2 < len) ? pgm_read_byte(&B64T[n & 63]) : '=';
  }
  out[j] = '\0';
}

static bool smtpWaitCode(WiFiClientSecure& c, int expect) {
  unsigned long t0 = millis();
  while (millis() - t0 < 15000UL) {
    while (c.available()) {
      String line = c.readStringUntil('\n');
      line.trim();
      if (line.length() < 3) continue;
      int code = line.substring(0, 3).toInt();
      if (code == expect && (line.length() == 3 || line.charAt(3) == ' ')) return true;
      if (code >= 400) return false;
    }
    delay(10);
  }
  return false;
}

bool sendSmtpEmail(const char* subject, const char* body) {
  if (WiFi.status() != WL_CONNECTED) return false;

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(15000);

  if (!client.connect(SMTP_HOST, SMTP_PORT)) return false;
  if (!smtpWaitCode(client, 220)) { client.stop(); return false; }

  client.println(F("EHLO upac-eben-ezer"));
  if (!smtpWaitCode(client, 250)) { client.stop(); return false; }

  client.println(F("AUTH LOGIN"));
  if (!smtpWaitCode(client, 334)) { client.stop(); return false; }

  char b64[128];
  base64Encode(SMTP_EMAIL, b64, sizeof(b64));
  client.println(b64);
  if (!smtpWaitCode(client, 334)) { client.stop(); return false; }

  base64Encode(SMTP_APP_PASSWORD, b64, sizeof(b64));
  client.println(b64);
  if (!smtpWaitCode(client, 235)) { client.stop(); return false; }

  char cmd[160];
  snprintf(cmd, sizeof(cmd), "MAIL FROM:<%s>", SMTP_EMAIL);
  client.println(cmd);
  if (!smtpWaitCode(client, 250)) { client.stop(); return false; }

  snprintf(cmd, sizeof(cmd), "RCPT TO:<%s>", ALERT_EMAIL_TO);
  client.println(cmd);
  if (!smtpWaitCode(client, 250)) { client.stop(); return false; }

  client.println(F("DATA"));
  if (!smtpWaitCode(client, 354)) { client.stop(); return false; }

  client.print(F("From: "));
  client.print(INSTITUTION);
  client.print(F(" <"));
  client.print(SMTP_EMAIL);
  client.println(F(">"));
  client.print(F("To: "));
  client.println(ALERT_EMAIL_TO);
  client.print(F("Subject: "));
  client.println(subject);
  client.println(F("Content-Type: text/plain; charset=UTF-8"));
  client.println();
  client.println(body);
  client.println(F("."));

  if (!smtpWaitCode(client, 250)) { client.stop(); return false; }

  client.println(F("QUIT"));
  client.stop();
  return true;
}
