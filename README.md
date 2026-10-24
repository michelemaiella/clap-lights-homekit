# Clap Lights for Apple HomeKit

Turn the lights of a room on and off with a **hand clap**, using a €25 microcontroller that appears in Apple's Home app as a native HomeKit accessory, with no cloud, bridge or extra app.

The device listens through its microphone, recognises a clap in real time and tells HomeKit to toggle the lights. Doors slamming, voices, drawers and knocks on furniture are ignored.

## How it works

```
 clap  ──▶  microphone  ──▶  signal processing  ──▶  HomeKit switch  ──▶  HomePod (hub)  ──▶  lights & smart plugs
            (Atom Echo)      (runs on the ESP32)      (HomeSpan)          runs automations
```

1. **Audio capture.** The ESP32 reads the PDM microphone at 16 kHz in 1 ms blocks.
2. **Filtering.** A DC-blocking filter and a first-order high-pass filter (~2 kHz) separate the sharp, high-frequency part of the sound from the low "thud".
3. **Event detection.** An adaptive noise floor tracks the room's background level, and any block that rises well above it starts an *event*.
4. **Classification.** When the event ends, three features are computed:
   - **peak**: how loud it is;
   - **duration**: how long it lasts;
   - **high-frequency ratio**: the share of energy above ~2 kHz.

   A clap is loud, short and bright. Everything else fails at least one test.
5. **Debounce.** After a command, every sound is ignored for 1.5 s, which removes the room's echo (it arrives ~100 ms after each clap).
6. **HomeKit.** The device exposes a HomeKit *Switch*. Each clap flips its state, and two Home automations ("turns on" → lights on, "turns off" → lights off) do the rest, so lights of any brand can be controlled.

## Calibration

The thresholds were not guessed: they came from measurements taken in the actual room with a dedicated calibration sketch (`calibration/`), which prints the features of every sound it hears.

| Sound | Peak | Duration | High-freq. ratio | Result |
|---|---|---|---|---|
| **Hand clap** (1–2.5 m) | 1,400 – 7,700 | 16 – 76 ms | **25 – 45 %** | ✅ accepted |
| Door closing | 780 – 4,200 | 61 – 181 ms | **1 – 3 %** | ❌ rejected |
| Knock on furniture | 970 – 1,600 | 1 – 16 ms | 2 – 3 % | ❌ rejected |
| Voice, drawer | 150 – 650 | – | 3 – 19 % | ❌ rejected |
| Background noise | 6 – 18 | – | – | – |

The key insight is that **loudness alone cannot separate a clap from a door**: the two overlap. Their frequency content, however, does not overlap at all, so the high-frequency ratio is what makes the classifier reliable.

Final thresholds: peak ≥ 1,000 · duration ≤ 90 ms · high-frequency ratio ≥ 20 %.

## Hardware

| Part | Notes | Approx. cost |
|---|---|---|
| [M5Stack Atom Echo](https://docs.m5stack.com/en/atom/atomecho) | ESP32-PICO-D4, Wi-Fi, PDM microphone (SPM1423), RGB LED, all in a 24 mm case | €25 |
| USB-C cable + any 5 V USB charger | Use a USB-A → USB-C cable | – |
| HomeKit home hub | HomePod, HomePod mini or Apple TV, needed to run automations | – |

No soldering, breadboard or enclosure is required.

## Repository structure

```
firmware/battito_singolo/       final firmware (clap → HomeKit switch)
  battito_singolo.ino
  secrets.example.h             template for Wi-Fi credentials and pairing code
calibration/taratura_schiocco/  calibration sketch: prints peak, duration, high-freq. ratio
```

## Setup

1. **Arduino IDE.** Install the board package **esp32 by Espressif Systems** (3.x) and the library **HomeSpan**.
2. **Credentials.** In `firmware/battito_singolo/`, copy `secrets.example.h` to `secrets.h` and fill in your 2.4 GHz Wi-Fi name, password and pairing code. `secrets.h` is excluded by `.gitignore`.
3. **Board settings.** Board **M5Atom**, Partition Scheme **Huge APP (3MB No OTA)**, Upload Speed **500000** (the default 1,500,000 fails on macOS).
4. **Upload**, then open the Serial Monitor at 115200 baud and wait for `PLEASE PAIR WITH HOMEKIT APP`.
5. **Pair.** In the Home app tap **+ → Add Accessory → More options** and enter the pairing code.
6. **Automations.** Create two: *Battito Interruttore turns on → lights on* and *turns off → lights off*.

The LED flashes green each time a clap is recognised.

### Tuning for another room

Upload the calibration sketch, then clap, close the door, talk and so on while watching the Serial Monitor. Adjust `PICCO_MIN`, `DURATA_MAX` and `ACUTI_MIN` at the top of the firmware to fit your numbers.

## Lessons learned

- **Measure first.** The first thresholds missed 3 out of 5 claps; only real data from the room made the detector reliable.
- **Every clap has an echo** ~60–100 ms later, about ten times weaker. Without a debounce window it would count as a second clap.
- **HomeKit button events vs. switches.** A *Stateless Programmable Switch* sent its events correctly (verified in the HAP logs), but the hub did not run the button actions. Exposing a regular *Switch* and using standard automations solved it.
- **Upload speed.** The USB-serial chip of the Atom Echo cannot handle 1.5 Mbaud on macOS; 500 kbaud is stable.

## Possible improvements

- Recognise **finger snaps** (weaker and brighter than claps) with a second calibration.
- Replace the hand-tuned thresholds with a small on-device ML model (e.g. Edge Impulse).
- Use the Atom's button as a manual toggle or a "mute" switch.

## Built with

[HomeSpan](https://github.com/HomeSpan/HomeSpan) · ESP32 Arduino core · Arduino IDE

## License

MIT
