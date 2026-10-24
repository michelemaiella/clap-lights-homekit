// Taratura schiocco - M5Stack Atom Echo
// Ascolta il microfono e, per ogni suono forte, stampa sul Monitor seriale:
//   picco   = quanto e' forte
//   salita  = quanti ms impiega a raggiungere il massimo (schiocco: 1-3 ms)
//   durata  = quanto dura (schiocco: pochi ms; porta: di piu')
//   acuti   = % di energia sulle frequenze alte (schiocco alto, porta bassa)
// Requisiti: scheda "esp32" di Espressif versione 3.x nell'Arduino IDE.

#include <ESP_I2S.h>

I2SClass i2s;

const int PIN_MIC_CLK  = 33;   // Atom Echo: clock microfono PDM
const int PIN_MIC_DATA = 23;   // Atom Echo: dati microfono
const int SAMPLE_RATE  = 16000;
const int BLOCK        = 16;   // 16 campioni = 1 ms

// filtri
float dcMean = 0;              // togli componente continua
float hpPrevIn = 0, hpPrevOut = 0;
const float HP_A = 0.56f;      // passa-alto ~2 kHz

// rumore di fondo e soglia
float noiseFloor = 200;
const float TRIGGER_X   = 6.0f;   // quante volte sopra il fondo per far partire un evento
const float MIN_TRIGGER = 150;    // soglia minima assoluta

// stato evento
bool inEvent = false;
unsigned long evStart = 0, evPeakTime = 0, quietSince = 0;
float evPeak = 0, evEnergyRaw = 0, evEnergyHp = 0;
unsigned long lastEventStart = 0;
unsigned long lastFloorPrint = 0;

void setup() {
  Serial.begin(115200);
  delay(500);
  i2s.setPinsPdmRx(PIN_MIC_CLK, PIN_MIC_DATA);
  if (!i2s.begin(I2S_MODE_PDM_RX, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)) {
    Serial.println("Errore: microfono non avviato");
    while (true) delay(1000);
  }
  Serial.println("Pronto. Schiocca, chiudi la porta, parla... e leggi i numeri.");
}

void loop() {
  int16_t buf[BLOCK];
  size_t got = i2s.readBytes((char*)buf, sizeof(buf)) / 2;
  if (got == 0) return;

  float peakHp = 0, eRaw = 0, eHp = 0;
  for (size_t i = 0; i < got; i++) {
    float x = buf[i];
    dcMean += 0.001f * (x - dcMean);
    float raw = x - dcMean;
    float hp = HP_A * (hpPrevOut + raw - hpPrevIn);
    hpPrevIn = raw; hpPrevOut = hp;
    eRaw += raw * raw;
    eHp  += hp * hp;
    if (fabsf(hp) > peakHp) peakHp = fabsf(hp);
  }

  unsigned long now = millis();
  float trigger = max(MIN_TRIGGER, noiseFloor * TRIGGER_X);

  if (!inEvent) {
    noiseFloor += 0.002f * (peakHp - noiseFloor);   // fondo che si adatta lentamente
    if (peakHp > trigger) {
      inEvent = true;
      evStart = evPeakTime = quietSince = now;
      evPeak = peakHp; evEnergyRaw = eRaw; evEnergyHp = eHp;
    } else if (now - lastFloorPrint > 3000) {
      Serial.printf("  (fondo: %.0f  soglia: %.0f)\n", noiseFloor, trigger);
      lastFloorPrint = now;
    }
    return;
  }

  // dentro un evento
  evEnergyRaw += eRaw; evEnergyHp += eHp;
  if (peakHp > evPeak) { evPeak = peakHp; evPeakTime = now; }
  if (peakHp > evPeak * 0.25f) quietSince = now;

  if (now - quietSince > 30 || now - evStart > 1000) {   // finito
    unsigned long durata = quietSince - evStart + 1;
    float acuti = evEnergyRaw > 0 ? 100.0f * evEnergyHp / evEnergyRaw : 0;
    unsigned long dopo = lastEventStart ? evStart - lastEventStart : 0;
    Serial.printf("EVENTO  picco=%.0f  durata=%lums  acuti=%.0f%%  dopo=%lums\n",
                  evPeak, durata, acuti, dopo);
    lastEventStart = evStart;
    inEvent = false;
    lastFloorPrint = now;
  }
}
