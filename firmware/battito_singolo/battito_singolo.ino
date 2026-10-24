// Battito singolo -> HomeKit - M5Stack Atom Echo
// Un battito di mani cambia stato a un interruttore HomeKit.
// Nell'app Casa: "si accende" -> luci accese, "si spegne" -> luci spente.
// Requisiti: scheda esp32 (Espressif) 3.x, libreria "HomeSpan".
// Strumenti -> Partition Scheme -> "Huge APP (3MB No OTA/1MB SPIFFS)".

#include "HomeSpan.h"
#include <ESP_I2S.h>

// ======== CREDENZIALI ========
// Copia secrets.example.h in secrets.h e inserisci i tuoi dati (secrets.h non va su GitHub).
#include "secrets.h"

// ======== SOGLIE (tarate sulla tua camera) ========
float PICCO_MIN   = 1000;   // battiti 1.400-7.700; voce e cassetto sotto 650
int   DURATA_MAX  = 90;     // ms: battiti 16-76 ms
float ACUTI_MIN   = 20;     // %: battiti 25-45%; porta e colpi sul mobile 1-4%
int   RIPOSO      = 1500;   // ms di blocco dopo aver acceso/spento

// ======== HARDWARE ========
const int PIN_MIC_CLK = 33, PIN_MIC_DATA = 23, PIN_LED = 27;
const int SAMPLE_RATE = 16000, BLOCK = 16;   // 16 campioni = 1 ms
I2SClass i2s;

volatile bool premiInterruttore = false;

// ---------- accessorio HomeKit ----------
// Interruttore che cambia stato a ogni battito e resta cosi'.
// Nell'app Casa due automazioni:
//   "Quando Battito si accende" -> luci Accese
//   "Quando Battito si spegne"  -> luci Spente
struct InterruttoreBattito : Service::Switch {
  SpanCharacteristic *acceso;
  InterruttoreBattito() : Service::Switch() {
    acceso = new Characteristic::On(false);
  }
  boolean update() override { return true; }   // si puo' cambiare anche dall'app
  void loop() override {
    if (premiInterruttore) {
      premiInterruttore = false;
      acceso->setVal(!acceso->getVal());
    }
  }
};

// ---------- led ----------
void led(uint8_t r, uint8_t g, uint8_t b) { rgbLedWrite(PIN_LED, r, g, b); }

// ---------- rilevamento ----------
float dcMean = 0, hpPrevIn = 0, hpPrevOut = 0;
const float HP_A = 0.56f;
float noiseFloor = 200;
bool inEvent = false;
unsigned long evStart, evPeakTime, quietSince;
float evPeak, evEnergyRaw, evEnergyHp;
unsigned long ultimoComando = 0, ledOff = 0;

void suonoFinito(unsigned long now) {
  unsigned long durata = quietSince - evStart + 1;
  float acuti = evEnergyRaw > 0 ? 100.0f * evEnergyHp / evEnergyRaw : 0;
  bool battito = evPeak >= PICCO_MIN && durata <= (unsigned long)DURATA_MAX && acuti >= ACUTI_MIN;

  // dopo un comando ignora tutto per RIPOSO ms (eco compresa)
  if (now - ultimoComando < (unsigned long)RIPOSO) return;

  Serial.printf("%s  picco=%.0f durata=%lums acuti=%.0f%%\n",
                battito ? "BATTITO " : "altro   ", evPeak, durata, acuti);
  if (!battito) return;

  Serial.println(">>> BATTITO: luci!");
  premiInterruttore = true;
  ultimoComando = now;
  led(0, 40, 0); ledOff = now + 400;      // verde
}

void ascolta() {
  int16_t buf[BLOCK];
  size_t got = i2s.readBytes((char*)buf, sizeof(buf)) / 2;
  if (!got) return;

  float peakHp = 0, eRaw = 0, eHp = 0;
  for (size_t i = 0; i < got; i++) {
    float x = buf[i];
    dcMean += 0.001f * (x - dcMean);
    float raw = x - dcMean;
    float hp = HP_A * (hpPrevOut + raw - hpPrevIn);
    hpPrevIn = raw; hpPrevOut = hp;
    eRaw += raw * raw; eHp += hp * hp;
    if (fabsf(hp) > peakHp) peakHp = fabsf(hp);
  }

  unsigned long now = millis();
  if (ledOff && now > ledOff) { led(0, 0, 0); ledOff = 0; }

  float trigger = max(800.0f, noiseFloor * 6.0f);
  if (!inEvent) {
    noiseFloor += 0.002f * (peakHp - noiseFloor);
    if (peakHp > trigger) {
      inEvent = true;
      evStart = evPeakTime = quietSince = now;
      evPeak = peakHp; evEnergyRaw = eRaw; evEnergyHp = eHp;
    }
    return;
  }
  evEnergyRaw += eRaw; evEnergyHp += eHp;
  if (peakHp > evPeak) { evPeak = peakHp; evPeakTime = now; }
  if (peakHp > evPeak * 0.25f) quietSince = now;
  if (now - quietSince > 30 || now - evStart > 1000) { inEvent = false; suonoFinito(now); }
}

void setup() {
  Serial.begin(115200);
  led(0, 0, 0);

  i2s.setPinsPdmRx(PIN_MIC_CLK, PIN_MIC_DATA);
  if (!i2s.begin(I2S_MODE_PDM_RX, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)) {
    Serial.println("Errore: microfono non avviato");
    led(40, 0, 0);
  }

  homeSpan.setWifiCredentials(WIFI_NOME, WIFI_PASSWORD);
  homeSpan.setPairingCode(CODICE_HOMEKIT);
  homeSpan.begin(Category::Switches, "Battito Interruttore");

  new SpanAccessory();
    new Service::AccessoryInformation();
      new Characteristic::Identify();
      new Characteristic::Name("Battito");
    new InterruttoreBattito();

  homeSpan.autoPoll();   // HomeKit gira in parallelo, il loop resta per il microfono
}

void loop() {
  ascolta();
}
