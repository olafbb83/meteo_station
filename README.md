# 🌦️ Advanced Smart Meteo Station (ESP32-S3 + BME280)

A professional-grade, multi-cloud IoT Weather Station powered by an **ESP32-S3**. This station captures high-precision local climate metrics (Temperature, Humidity, Barometric Pressure, and Altitude) and presents them across four independent interfaces: a physical OLED display, a local web server interface, long-term historical cloud analytics via **ThingSpeak**, and a beautiful, real-time native smartphone app using **Blynk**.

Featuring a **bulletproof software onboarding captive portal** and an **RTC hardware double-reset escape hatch**, this device is completely dynamic—built to be safely gifted, moved, and reconfigured across different Wi-Fi networks and private cloud accounts without ever needing to touch a line of code.

---

## 🚀 Key Features

* **Quad-Interface Telemetry:**
    * **On-Device OLED:** Animated 128x64 display tracking metrics, a beating pulse heartbeat icon, local IP address, network time synchronization (NTP), and live barometric trend indicators (`^` / `v`).
    * **Local Web Server (`192.168.4.1`):** A custom dark-themed local dashboard showing responsive climate cards alongside dynamically rendered **24-hour SVG line charts** (generated natively on the ESP32 chip).
    * **ThingSpeak Analytics:** Long-term global historical database logging for tracking multi-week weather patterns.
    * **Blynk Mobile App:** Premium smartphone dashboard featuring live-updating gauges, status lights, and real-time interactive charting over cellular data.
* **Commercial Onboarding Portal:** If the station cannot connect to a saved Wi-Fi network, it automatically drops into Access Point Mode, broadcasting its own secure configuration hotspot network (`Meteo-Station-Setup`) allowing any smartphone to dynamically input local Wi-Fi credentials, a custom ThingSpeak API Key, and a custom Blynk Auth Token.
* **Hardware Escape Hatch (Double-Reset Detection):** Utilizing deep processor RTC memory registers, pressing the hardware reset button twice rapidly (within 3.5 seconds) completely wipes saved credentials from internal Flash memory (`Preferences`), serving as a manual factory reset.

---

## 🛠️ Hardware Requirements

* **Microcontroller:** ESP32-S3 (Dual-Core XTensa, 2.4GHz Wi-Fi)
* **Sensor:** Bosch BME280 Environmental Sensor (I2C Variant)
* **Display:** SSD1306 128x64 I2C OLED display (Yellow/Blue split-zone ideal)
* **Wiring Topology (I2C Bus):**
    * `GPIO 8` ➡️ **SDA** (Shared between Sensor & OLED)
    * `GPIO 9` ➡️ **SCL** (Shared between Sensor & OLED)
    * `3.3V`   ➡️ **VCC**
    * `GND`    ➡️ **GND**

---

## 💾 Software Installation & Dependencies

To compile this project, ensure you have the **Arduino IDE** or **VS Code + PlatformIO** configured for ESP32 boards. Install the following libraries through your library manager:

1.  `Blynk` (by Volodymyr Shymanskyy)
2.  `Adafruit BME280 Library`
3.  `Adafruit SSD1306`
4.  `Adafruit GFX Library`

### ⚠️ Preprocessor Order Note
To compile cleanly without library compilation failures, your main `.ino` file must keep the `#define BLYNK_...` templates grouped at the absolute **top of the document** preceding any header `#include` statements.

---

## 📖 User Configuration & Operating Manual

### 1. Initial Out-of-the-Box Setup
1. Turn the Meteo Station on by plugging it into a USB port or power brick.
2. The OLED display will show **`1. CONNECT TO WI-FI: Meteo-Station-Setup`** and **`2. GO TO: 192.168.4.1`**.
3. Open your smartphone's Wi-Fi settings, connect to the **Meteo-Station-Setup** network, and open a web browser to `192.168.4.1`.
4. Enter your home Wi-Fi Network Name (SSID), Password, your ThingSpeak Write API key, and your Blynk Auth Token.
5. Click **Connect Station**. The device will save your details directly into its secure storage vault, close the hotspot, and reboot into active tracking mode.

### 2. Cloud Integration Architecture
To view data across your global dashboards, assign your platform endpoints as follows:

* **ThingSpeak Configuration:**
    * Enable **Field 1** (Temperature), **Field 2** (Humidity), and **Field 3** (Pressure) in your channel settings page.
* **Blynk App Pin Map:**
    * `Virtual Pin V1` ➡️ Temperature (Double/Float)
    * `Virtual Pin V2` ➡️ Humidity (Integer)
    * `Virtual Pin V3` ➡️ Pressure (Double/Float)

### 3. Factory Reset Escape Hatch
If you change your home router, get a new password, or want to gift the physical device to a friend:
1. Locate the physical **RST** button on the ESP32 board (or unplug/replug the power cable).
2. Press the reset button, wait 1 second, and press it **again** immediately.
3. The OLED screen will safely interrupt the sequence, flash **`SETTINGS CLEARED! Opening Portal...`**, wipe the internal storage flash memory clean, and re-broadcast the onboarding portal configuration network.

---

## 🔒 Safety & Stability Features
* **Rate Limit Protection:** Free cloud accounts restrict updates. The background logging system relies on non-blocking `millis()` delta tracking timers set to 5-minute segments (`300000ms`), preventing account rate blocking while keeping your local 2.5-second OLED heart-pulse fluid.
* **String Fail-safes:** Leaving cloud inputs completely blank inside the setup dashboard causes the background state controller to automatically shut down outbound internet traffic loops, preventing memory overruns and saving processor cycles.
