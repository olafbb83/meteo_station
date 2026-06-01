# 🌦️ Advanced Smart Meteo Station (ESP32-S3 + BME280 + VEML7700)

A professional-grade, multi-cloud IoT Weather Station powered by an **ESP32-S3**. Captures high-precision local climate metrics (Temperature, Humidity, Barometric Pressure, Altitude, Light Level, Dew Point, Feels Like) and presents them across four independent interfaces: a physical OLED display, a local web server dashboard, long-term cloud analytics via **ThingSpeak**, and a real-time smartphone app using **Blynk**.

Featuring a **bulletproof software onboarding captive portal**, an **RTC hardware double-reset escape hatch**, and a **multi-layer weather forecast engine** (Zambretti + lux + humidity + diurnal correction).

---

## 🚀 Key Features

* **Quad-Interface Telemetry:**
    * **On-Device OLED:** Animated 128x64 display with beating pulse heartbeat icon, live T/H/P/Altitude, NTP time, IP address, and barometric trend arrows (`^` / `v`).
    * **Local Web Server:** Dark-themed responsive dashboard with 7 metric cards and dynamically rendered **24-hour SVG line charts** for Temperature, Humidity, Pressure, and Light — all generated natively on the ESP32 chip.
    * **ThingSpeak Analytics:** Long-term cloud logging across 6 fields for multi-week weather pattern analysis.
    * **Blynk Mobile App:** Smartphone dashboard with live gauges on 6 virtual pins.

* **Multi-Layer Forecast Engine (Zambretti++):**
    * **5-level pressure trend** — distinguishes fast vs slow rises/falls (±1.5 and ±3.0 hPa/3h thresholds)
    * **Dual trend window** — 3h window for fast storm detection, 24h window for stable settled/improving patterns
    * **Lux cross-reference** — confirms or upgrades forecast using ambient light (night-aware, skips correction between 22:00–06:00)
    * **Humidity secondary signal** — high humidity flags fog/mist risk and rain certainty; low humidity confirms dry clearing
    * **Diurnal pressure correction** — 24-point hourly lookup table removes the natural atmospheric tide (~0.9 hPa amplitude) to eliminate false alarms

* **Commercial Onboarding Portal:** Drops into AP mode (`Meteo-Station-Setup`) when no Wi-Fi credentials are saved, allowing any smartphone to configure Wi-Fi, ThingSpeak API key, and Blynk token at `192.168.4.1`.

* **Hardware Escape Hatch (Double-Reset Detection):** Two resets within 3.5 seconds wipes all saved credentials from NVS flash and relaunches the onboarding portal.

---

## 🛠️ Hardware Requirements

* **Microcontroller:** ESP32-S3 (Dual-Core XTensa, 2.4GHz Wi-Fi)
* **Environmental Sensor:** Bosch BME280 (I2C, address `0x76`)
* **Light Sensor:** Adafruit VEML7700 (I2C, address `0x10`)
* **Display:** SSD1306 128x64 I2C OLED (address `0x3C`, Yellow/Blue split-zone ideal)

### Wiring — Shared I2C Bus

| Signal | ESP32-S3 Pin | Connected To |
|---|---|---|
| SDA | GPIO 8 | BME280 + VEML7700 + OLED |
| SCL | GPIO 9 | BME280 + VEML7700 + OLED |
| VCC | 3.3V | All three devices |
| GND | GND | All three devices |

> **VEML7700 note:** Connect VIN to 3.3V. Leave the 3Vo pin unconnected.

---

## 💾 Software Installation & Dependencies

Configure **Arduino IDE** or **VS Code + PlatformIO** for ESP32 boards, then install:

1. `Blynk` (by Volodymyr Shymanskyy)
2. `Adafruit BME280 Library`
3. `Adafruit VEML7700 Library`
4. `Adafruit SSD1306`
5. `Adafruit GFX Library`

### ⚠️ Preprocessor Order Note
The `#define BLYNK_TEMPLATE_ID` and `#define BLYNK_TEMPLATE_NAME` macros must appear at the **absolute top** of the `.ino` file, before any `#include` statements.

---

## ⚙️ Configuration Files

### `secrets.h`
Compile-time credential fallbacks (used if no credentials are saved in NVS flash):
```cpp
#define SECRET_SSID        "your_wifi_ssid"
#define SECRET_PASS        "your_wifi_password"
#define SECRET_TS_KEY      "your_thingspeak_write_key"
#define SECRET_BLYNK_TOKEN "your_blynk_auth_token"
```
> ⚠️ This file is gitignored. Never commit real credentials.

### `settings.h`
Local tuning parameters — adjust without touching firmware logic:
```cpp
#define LOCAL_PRESSURE_OFFSET  (-16.25)  // hPa offset for local elevation
#define LUX_BRIGHT_SUN         10000.0   // lux threshold for direct sun
#define LUX_OVERCAST             500.0   // lux threshold for heavy overcast
#define LUX_NIGHT                 10.0   // lux threshold for night
#define HUM_HIGH                  85.0   // % — high humidity modifier trigger
#define HUM_LOW                   40.0   // % — low humidity modifier trigger
#define DIURNAL_CORRECTION { ... }       // 24-point hourly pressure correction table
```

---

## 📖 User Configuration & Operating Manual

### 1. Initial Out-of-the-Box Setup
1. Power on the station via USB or power brick.
2. OLED shows: **`1. CONNECT TO WI-FI: Meteo-Station-Setup`** and **`2. GO TO: 192.168.4.1`**.
3. Connect your smartphone to the `Meteo-Station-Setup` hotspot and open `192.168.4.1`.
4. Enter Wi-Fi SSID, Password, ThingSpeak Write API Key, and Blynk Auth Token.
5. Tap **Connect Station** — credentials are saved to NVS flash, the hotspot closes, and the device reboots into active mode.

### 2. Cloud Integration Architecture

**ThingSpeak — enable all 6 fields:**

| Field | Metric | Format |
|---|---|---|
| Field 1 | Temperature | x.x °C |
| Field 2 | Humidity | x.x % |
| Field 3 | Pressure | x.xx hPa |
| Field 4 | Dew Point | x.x °C |
| Field 5 | Feels Like (Heat Index) | x.x °C |
| Field 6 | Light Level | x lux |

**Blynk App — Virtual Pin Map:**

| Pin | Metric | Widget Type |
|---|---|---|
| V1 | Temperature | Gauge / Float |
| V2 | Humidity | Gauge / Float |
| V3 | Pressure | Gauge / Float |
| V4 | Dew Point | Gauge / Float |
| V5 | Feels Like | Gauge / Float |
| V6 | Light Level | Gauge / Integer |

### 3. Factory Reset Escape Hatch
1. Press the **RST** button on the ESP32.
2. Within 3.5 seconds, press **RST** again.
3. OLED flashes **`SETTINGS CLEARED! Opening Portal...`**, wipes NVS flash, and relaunches the onboarding portal.

---

## 🔬 Derived Metrics

| Metric | Formula | Notes |
|---|---|---|
| **Dew Point** | Magnus formula | Accurate to ±0.35°C |
| **Feels Like** | Steadman full regression (NOAA/NWS) | Returns actual temp below 27°C |
| **Zambretti Forecast** | Multi-layer engine (see below) | Updates every 5 minutes |

### Zambretti Forecast Engine

The forecast pipeline runs every 5 minutes:

```
Raw Pressure
  → Diurnal correction (24-point hourly table)
  → calculateZambretti() [3h window]  ─┐
  → calculateZambretti() [24h window] ─┴→ pick stronger signal
  → applyLuxModifier()   (night-aware)
  → applyHumidityModifier()
  → zambrettiForecast string
```

Forecast strings include: `Settled Fine`, `Settled Fine, Sunny`, `Settled Fine, Dry`, `Partly Cloudy`, `Fog/Mist Risk`, `Fair, Worsening`, `Showers Likely`, `Rain Expected`, `Rain & Wind`, `Storm Approaching!`, `Deteriorating Rapidly`, `Fairing Up`, `Improving Rapidly`, `Clearing Storm`, `High Pressure, Sunny`, `High Pressure, Cloudy`, and more.

---

## 🔒 Safety & Stability Features
* **Rate Limit Protection:** Cloud updates use non-blocking `millis()` timers at 5-minute intervals (`300000ms`), respecting free-tier ThingSpeak and Blynk rate limits while keeping the OLED refresh fluid at 2.5 seconds.
* **String Fail-safes:** Blank API key or Blynk token fields disable outbound cloud traffic for that service, preventing memory overruns.
* **Sensor Fault Tolerance:** Missing BME280 or VEML7700 at boot logs an error to Serial and continues — the station won't crash if a sensor is disconnected.
