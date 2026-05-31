#define BLYNK_TEMPLATE_ID "TMPL5OdiOe_Aj"
#define BLYNK_TEMPLATE_NAME "OB meteo station"

#include "secrets.h"
#include "settings.h"
#define BLYNK_AUTH_TOKEN SECRET_BLYNK_TOKEN

// 2. NOW INCLUDE LIBRARIES Safely
#include <WiFi.h>
#include <WebServer.h>
#include <BlynkSimpleEsp32.h> 
#include <time.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Preferences.h>

// 3. HARDWARE CONFIGURATIONS BELOW
#define I2C_SDA 8
#define I2C_SCL 9
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define SCREEN_ADDRESS 0x3C
#define SEALEVELPRESSURE_HPA (1013.25)
#define MAX_HISTORY_POINTS 144

// This variable survives CPU reboots and handles our escape hatch!
RTC_DATA_ATTR int bootCountSincePowerOn = 0;

// --- Default fallback configuration credentials ---
// Leave blank to test your portal setup configuration cleanly!
const char* defaultSSID = SECRET_SSID;
const char* defaultPASS = SECRET_PASS;
String tsAPIKey = SECRET_TS_KEY;

// --- ThingSpeak Cloud Configuration ---
const char* thingSpeakAddress = "api.thingspeak.com";
unsigned long lastCloudUpdateTime = 0;
const unsigned long cloudUpdateInterval = 300000; // 5 minutes in milliseconds

// --- Automatic Wi-Fi Onboarding Configuration ---
bool isConfigured = false;
unsigned long portalStartTime = 0;
const char* apSSID = "Meteo-Station-Setup";

Preferences preferences;
String savedSSID = "";
String savedPASS = "";

String blynkAuthKey = BLYNK_AUTH_TOKEN;

float tempHistory[MAX_HISTORY_POINTS];
float presHistory[MAX_HISTORY_POINTS];
float humHistory[MAX_HISTORY_POINTS];
int historyCount = 0;
unsigned long lastHistoryLogTime = 0;
const unsigned long logInterval = 300000; 

Adafruit_BME280 bme;
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
WebServer server(80);

// Global variables to keep track of pressure trends
float lastPressure = 0.0F;
String trendArrow = "->";

// Global variables for Wi-Fi and Time
String ipStr = "0.0.0.0";
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 3600;
const int daylightOffset_sec = 3600;

unsigned long lastAnimationToggle = 0;
bool heartIsFilled = true;

// --- Zambretti Forecast Engine Config ---
#define TREND_WINDOW_SIZE 36 // 3 hours of data (12 samples/hour * 3)
float pressureHistory[TREND_WINDOW_SIZE];
int zambrettiCount = 0;

enum Trend { TREND_FALLING, TREND_STEADY, TREND_RISING };
String zambrettiForecast = "Calibrating...";

// --- PROTOTYPES / FORWARD DECLARATIONS ---
void updateDisplayConnecting();
void updateDisplayPortal();
void handlePortalRoot();
void handlePortalSave();
void handleRoot();
void initWiFi();
void logHistoryData(float currentTemp, float currentPres, float currentHum);
String generateSVGChart(float data[], int count, String strokeColor, float minVal, float maxVal, String unit);

// --- SVG CHART GENERATOR ENGINE ---
String generateSVGChart(float data[], int count, String strokeColor, float minVal, float maxVal, String unit) {
  String svg = "<svg viewBox='0 0 320 100' width='100%' height='100'><g>";
  
  float topVal = maxVal - ((maxVal - minVal) * 10.0F / 80.0F);
  float midVal = maxVal - ((maxVal - minVal) * 50.0F / 80.0F);
  float botVal = maxVal - ((maxVal - minVal) * 90.0F / 80.0F);

  svg += "<line x1='40' y1='10' x2='320' y2='10' stroke='#334155' stroke-dasharray='4'/>";
  svg += "<line x1='40' y1='50' x2='320' y2='50' stroke='#334155' stroke-dasharray='4'/>";
  svg += "<line x1='40' y1='90' x2='320' y2='90' stroke='#334155' stroke-dasharray='4'/>";
  
  svg += "<text x='35' y='13' fill='#64748b' font-size='9' font-family='sans-serif' text-anchor='end'>" + String(topVal, 0) + unit + "</text>";
  svg += "<text x='35' y='53' fill='#64748b' font-size='9' font-family='sans-serif' text-anchor='end'>" + String(midVal, 0) + unit + "</text>";
  svg += "<text x='35' y='93' fill='#64748b' font-size='9' font-family='sans-serif' text-anchor='end'>" + String(botVal, 0) + unit + "</text>";

  if (count > 1) {
    svg += "<polyline points='";
    float span = (maxVal - minVal == 0) ? 1.0F : (maxVal - minVal);
    for (int i = 0; i < count; i++) {
      float x = 40.0F + ((i * 280.0F) / (MAX_HISTORY_POINTS - 1));
      float y = 90.0F - ((data[i] - minVal) * 80.0F / span); 
      svg += String(x, 1) + "," + String(y, 1) + " ";
    }
    svg += "' stroke='" + strokeColor + "'/>";
  } else {
    svg += "<circle cx='45' cy='50' r='4' fill='" + strokeColor + "'/>";
  }
  svg += "</g></svg>";
  return svg;
}

String calculateZambretti(float currentPressure, float pressure3HoursAgo) {
    float delta = currentPressure - pressure3HoursAgo;
    Trend trend = TREND_STEADY;
    
    // Define thresholds: change greater than 1.5 hPa over 3 hours is significant
    if (delta <= -1.5) trend = TREND_FALLING;
    else if (delta >= 1.5) trend = TREND_RISING;

    // 1. FALLING PRESSURE (Weather worsening)
    if (trend == TREND_FALLING) {
        if (currentPressure > 1020 + LOCAL_PRESSURE_OFFSET) return "Fair, Worsening";
        if (currentPressure <= 1020 + LOCAL_PRESSURE_OFFSET && currentPressure > 1010 + LOCAL_PRESSURE_OFFSET) return "Showers Likely";
        if (currentPressure <= 1010 + LOCAL_PRESSURE_OFFSET && currentPressure > 1000 + LOCAL_PRESSURE_OFFSET) return "Rain, Wind";
        return "Storm Approaching!";
    }
    
    // 2. RISING PRESSURE (Weather improving)
    if (trend == TREND_RISING) {
        if (currentPressure < 1000 + LOCAL_PRESSURE_OFFSET) return "Clearing Storm";
        if (currentPressure >= 1000 + LOCAL_PRESSURE_OFFSET && currentPressure < 1015 + LOCAL_PRESSURE_OFFSET) return "Fairing Up";
        if (currentPressure >= 1015 + LOCAL_PRESSURE_OFFSET && currentPressure < 1025 + LOCAL_PRESSURE_OFFSET) return "Settled Fine";
        return "High Pressure, Sunny";
    }
    
    // 3. STEADY PRESSURE (Weather staying the same)
    if (currentPressure > 1015 + LOCAL_PRESSURE_OFFSET) return "Settled Fine";
    if (currentPressure <= 1015 + LOCAL_PRESSURE_OFFSET && currentPressure > 1008 + LOCAL_PRESSURE_OFFSET) return "Partly Cloudy";
    return "Unsettled/Rainy";
}

void updateThingSpeakCloud(float t, float h, float p) {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClient client;
  if (client.connect(thingSpeakAddress, 80)) {
    
    String tsData = "api_key=" + String(tsAPIKey) + 
                    "&field1=" + String(t, 1) + 
                    "&field2=" + String((int)h) + 
                    "&field3=" + String(p, 0);

    client.print("POST /update HTTP/1.1\r\n");
    client.print("Host: api.thingspeak.com\r\n");
    client.print("Connection: close\r\n");
    client.print("Content-Type: application/x-www-form-urlencoded\r\n");
    client.print("Content-Length: " + String(tsData.length()) + "\r\n");
    client.print("\r\n"); 
    client.print(tsData); 
    client.print("\r\n");

    Serial.println("[CLOUD] Data sent. Waiting for server response...");
    
    // --- FIX: WAIT FOR SERVER TO ACTUALLY REPLY ---
    unsigned long timeout = millis();
    while (client.available() == 0) {
      if (millis() - timeout > 5000) { // 5 second safety timeout
        Serial.println("[ERROR] ThingSpeak response timeout!");
        client.stop();
        return;
      }
      delay(10); 
    }
    
    // Now that data is available, read the first line (the HTTP Status)
    if(client.available()){
      String statusLine = client.readStringUntil('\r');
      Serial.print("[SERVER HTTP STATUS]: ");
      Serial.println(statusLine);
    }
    
    // Skip remaining headers and read the final response body line (the entry ID)
    String lastLine = "";
    while(client.available()){
      lastLine = client.readStringUntil('\n');
    }
    lastLine.trim();
    Serial.print("[THINGSPEAK ENTRY ID]: ");
    Serial.println(lastLine);
    
    client.stop();
  } else {
    Serial.println("[ERROR] Cloud connection to ThingSpeak failed.");
  }
}

void handlePortalRoot() {
  String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<style>body{font-family:sans-serif; background:#0f172a; color:#f8fafc; padding:20px; text-align:center;}";
  html += "input{width:100%; max-width:300px; padding:12px; margin:8px 0; border-radius:8px; border:1px solid #334155; background:#1e293b; color:#fff; box-sizing:border-box;}";
  html += "button{background:#38bdf8; color:#0f172a; font-weight:bold; border:none; padding:12px 30px; border-radius:8px; margin-top:10px; cursor:pointer;}</style></head><body>";
  html += "<h2>Meteo Station Setup</h2><p>Enter your local Wi-Fi & Cloud details:</p>";
  html += "<form action='/save' method='POST'>";
  html += "<input type='text' name='ssid' placeholder='Wi-Fi Network Name (SSID)' required><br>";
  html += "<input type='password' name='pass' placeholder='Wi-Fi Password'><br>";
  html += "<input type='text' name='tskey' placeholder='ThingSpeak Write API Key'><br>";
  html += "<input type='text' name='blynkkey' placeholder='Blynk Auth Token'><br>";
  html += "<button type='submit'>Connect Station</button></form></body></html>";
  server.send(200, "text/html", html);
}

void handlePortalSave() {
  if (server.hasArg("ssid")) {
    String reqSSID  = server.arg("ssid");
    String reqPASS  = server.arg("pass");
    String reqKEY   = server.arg("tskey");
    String reqBLYNK = server.arg("blynkkey"); // Capture the Blynk token

    preferences.begin("wifi-creds", false);
    preferences.putString("ssid", reqSSID);
    preferences.putString("password", reqPASS);
    
    // --- ThingSpeak Key Management ---
    if (reqKEY != "") {
      preferences.putString("tskey", reqKEY);
      tsAPIKey = reqKEY;
    } else {
      preferences.putString("tskey", "");
      tsAPIKey = "";
    }

    // --- Blynk Token Management ---
    if (reqBLYNK != "") {
      preferences.putString("blynkkey", reqBLYNK);
      blynkAuthKey = reqBLYNK; // Update our active runtime variable
    } else {
      preferences.putString("blynkkey", "");
      blynkAuthKey = ""; // Intentionally blanked out by the user
    }
    
    preferences.end();

    String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    html += "<style>body{font-family:sans-serif; background:#0f172a; color:#f8fafc; padding:20px; text-align:center;}</style></head><body>";
    html += "<h2>Credentials Saved!</h2><p>The Meteo Station is now restarting to join your network.</p></body></html>";
    
    server.send(200, "text/html", html);
    delay(500); 
    server.close();
    WiFi.softAPdisconnect(true);
    delay(500);
    
    Serial.println("[SYSTEM] Rebooting safely now...");
    ESP.restart();
  }
}

void updateBlynkCloud(float t, float h, float p) {
  // Check if Blynk engine is actively connected to the server
  if (Blynk.connected()) {
    Blynk.virtualWrite(V1, t);       // Send temp to pipe V1
    Blynk.virtualWrite(V2, (int)h);  // Send humidity to pipe V2
    Blynk.virtualWrite(V3, p);       // Send pressure to pipe V3
    Serial.println("[BLYNK CLOUD] Real-time metrics pushed to smartphone app!");
  }
}

void initWiFi() {
  preferences.begin("wifi-creds", false);
  savedSSID = preferences.getString("ssid", "");
  savedPASS = preferences.getString("password", "");
  
  // --- Read the saved ThingSpeak key if it exists ---
  String savedKEY = preferences.getString("tskey", "");
  if (savedKEY != "") {
    tsAPIKey = savedKEY;
    Serial.println("[SYSTEM] Loaded custom ThingSpeak API Key from flash.");
  }
  
  // --- Read the saved Blynk token if it exists ---
  String savedBLYNK = preferences.getString("blynkkey", "");
  if (savedBLYNK != "") {
    blynkAuthKey = savedBLYNK;
    Serial.println("[SYSTEM] Loaded custom Blynk Auth Token from flash.");
  }
  
  // Fallback check for default compilation credentials
  if (savedSSID == "" && String(defaultSSID) != "") {
    preferences.putString("ssid", defaultSSID);
    preferences.putString("password", defaultPASS);
    savedSSID = defaultSSID;
    savedPASS = defaultPASS;
  }
  
  preferences.end(); // Safely lock the storage engine
  
  // Crash protection check
  if (savedSSID == "") {
    Serial.println("[PORTAL] No credentials available. Skipping connection phase...");
  } else {
    WiFi.mode(WIFI_STA);
    Serial.println("[SYSTEM] Found saved Wi-Fi credentials. Attempting connection...");
    WiFi.begin(savedSSID.c_str(), savedPASS.c_str());
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
      updateDisplayConnecting(); 
      delay(500);
      attempts++;
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("[SUCCESS] Connected to home network!");
    isConfigured = true;
    ipStr = WiFi.localIP().toString();
    return;
  }

  // AP Fallback Activation
  Serial.println("[PORTAL] Launching Access Point Hotspot...");
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apSSID);
  
  server.on("/", handlePortalRoot);
  server.on("/save", handlePortalSave);
  server.begin();
  
  isConfigured = false;
}

void handleRoot() {
  float temp = bme.readTemperature();
  float hum = bme.readHumidity();
  float pres = bme.readPressure() / 100.0F;
  float alt = bme.readAltitude(SEALEVELPRESSURE_HPA);

  String html = "<!DOCTYPE html><html>";
  html += "<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">";
  html += "<title>Meteo Station</title>";
  html += "<style>";
  html += "body { font-family: 'Segoe UI', sans-serif; background-color: #0f172a; color: #f8fafc; margin: 0; padding: 15px; display: flex; flex-direction: column; align-items: center; }";
  html += ".header { text-align: center; margin-bottom: 15px; }";
  html += ".header h1 { margin: 0; color: #38bdf8; font-size: 26px; }";
  html += ".grid { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; width: 100%; max-width: 450px; margin-bottom: 15px; }";
  html += ".card { background: #1e293b; padding: 15px; border-radius: 12px; border-left: 4px solid #64748b; }";
  html += ".card.temp { border-left-color: #f59e0b; } .card.hum { border-left-color: #3b82f6; }";
  html += ".card.pres { border-left-color: #10b981; } .card.alt { border-left-color: #8b5cf6; }";
  html += ".label { font-size: 11px; text-transform: uppercase; color: #94a3b8; margin-bottom: 3px; }";
  html += ".value { font-size: 24px; font-weight: bold; }";
  html += ".unit { font-size: 14px; color: #94a3b8; margin-left: 2px; }";
  html += ".chart-container { background: #1e293b; width: 100%; max-width: 420px; border-radius: 12px; padding: 15px; margin-bottom: 15px; box-sizing: border-box; }";
  html += "h3 { margin-top: 0; margin-bottom: 10px; color: #94a3b8; font-size: 14px; text-transform: uppercase; text-align: center; }";
  html += "polyline { fill: none; stroke-width: 3; stroke-linecap: round; stroke-linejoin: round; }";
  html += "</style>";
  html += "<meta http-equiv='refresh' content='30'>"; 
  html += "</head><body>";

  html += "<div class='header'><h1>Meteo Station</h1><p style='margin:2px; color:#64748b; font-size:12px;'>Live & Offline Local Trends</p></div>";

  html += "<div class='grid'>";
  html += "<div class='card temp'><div class='label'>Temp</div><div class='value'>" + String(temp, 1) + "<span class='unit'>&deg;C</span></div></div>";
  html += "<div class='card hum'><div class='label'>Humidity</div><div class='value'>" + String((int)hum) + "<span class='unit'>%</span></div></div>";
  html += "<div class='card pres'><div class='label'>Pressure</div><div class='value'>" + String(pres, 0) + "<span class='unit'>hPa</span></div></div>";
  html += "<div class='card alt'><div class='label'>Altitude</div><div class='value'>" + String(alt, 0) + "<span class='unit'>m</span></div></div>";
  html += "</div>";

  html += "<div class='card' style='width:100%;max-width:450px;border-left-color:#e2e8f0;margin-bottom:15px;box-sizing:border-box;'>";
  html += "<div class='label'>Zambretti Forecast</div>";
  html += "<div class='value' style='font-size:18px;'>" + zambrettiForecast + "</div></div>";

  float minT = 20.0, maxT = 30.0;
  float minP = 980.0, maxP = 1020.0;
  float minH = 30.0, maxH = 70.0;

  if (historyCount > 0) {
    minT = tempHistory[0]; maxT = tempHistory[0];
    minP = presHistory[0]; maxP = presHistory[0];
    minH = humHistory[0]; maxH = humHistory[0];
    
    for(int i = 0; i < historyCount; i++) {
      if(tempHistory[i] < minT) minT = tempHistory[i]; if(tempHistory[i] > maxT) maxT = tempHistory[i];
      if(presHistory[i] < minP) minP = presHistory[i]; if(presHistory[i] > maxP) maxP = presHistory[i];
      if(humHistory[i] < minH) minH = humHistory[i];   if(humHistory[i] > maxH) maxH = humHistory[i];
    }
    
    minT -= 1.0; maxT += 1.0; 
    minP -= 2.0; maxP += 2.0; 
    minH -= 5.0; maxH += 5.0;
    
    if (minT == maxT) { minT -= 1.0; maxT += 1.0; }
    if (minP == maxP) { minP -= 2.0; maxP += 2.0; }
    if (minH == maxH) { minH -= 5.0; maxH += 5.0; }
  }

  html += "<div class='chart-container'><h3>24h Temperature History (&deg;C)</h3>" + generateSVGChart(tempHistory, historyCount, "#f59e0b", minT, maxT, "&deg;") + "</div>";
  html += "<div class='chart-container'><h3>24h Humidity History (%)</h3>" + generateSVGChart(humHistory, historyCount, "#3b82f6", minH, maxH, "%") + "</div>";
  html += "<div class='chart-container'><h3>24h Pressure History (hPa)</h3>" + generateSVGChart(presHistory, historyCount, "#10b981", minP, maxP, "hPa") + "</div>";

  html += "</body></html>";
  server.send(200, "text/html", html);
}

void logHistoryData(float currentTemp, float currentPres, float currentHum) {
  if (historyCount < MAX_HISTORY_POINTS) {
    tempHistory[historyCount] = currentTemp;
    presHistory[historyCount] = currentPres;
    humHistory[historyCount] = currentHum;
    historyCount++;
  } else {
    for (int i = 0; i < MAX_HISTORY_POINTS - 1; i++) {
      tempHistory[i] = tempHistory[i + 1];
      presHistory[i] = presHistory[i + 1];
      humHistory[i] = humHistory[i + 1];
    }
    tempHistory[MAX_HISTORY_POINTS - 1] = currentTemp;
    presHistory[MAX_HISTORY_POINTS - 1] = currentPres;
    humHistory[MAX_HISTORY_POINTS - 1] = currentHum;
  }
  Serial.println("[SYSTEM LOG] Captured history data point (Temp, Pres, Hum).");
}

void updateDisplayConnecting() {
  float temp = bme.readTemperature();
  float hum = bme.readHumidity();
  
  int tempWhole = (int)temp;
  int tempDec = (int)(abs(temp) * 10) % 10;
  
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  
  display.setTextSize(1); display.setCursor(0, 0); display.print("T:");
  display.setCursor(12, 0); display.setTextSize(2); display.print(tempWhole);
  
  int xOffset = (tempWhole >= 10 || tempWhole <= -10) ? 36 : 24;
  
  display.setTextSize(1);
  display.setCursor(xOffset, 8); display.print(","); display.print(tempDec);
  display.setCursor(xOffset + 6, 0); display.print("C"); 
  
  display.setTextSize(1); display.setCursor(76, 0); display.print("H:");
  display.setCursor(88, 0); display.setTextSize(2); display.print((int)hum); display.print("%");
  
  display.setCursor(22, 34);
  display.setTextSize(1);
  display.print("CONNECTING...");
  display.display();
}

void updateDisplayPortal() {
  float temp = bme.readTemperature();
  float hum = bme.readHumidity();
  
  int tempWhole = (int)temp;
  int tempDec = (int)(abs(temp) * 10) % 10;
  
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  
  // --- Yellow Zone Content ---
  display.setTextSize(1); display.setCursor(0, 0); display.print("T:");
  display.setCursor(12, 0); display.setTextSize(2); display.print(tempWhole);
  
  int xOffset = (tempWhole >= 10 || tempWhole <= -10) ? 36 : 24;
  
  display.setTextSize(1);
  display.setCursor(xOffset, 8); display.print(","); display.print(tempDec);
  display.setCursor(xOffset + 6, 0); display.print("C"); 
  
  display.setTextSize(1); display.setCursor(76, 0); display.print("H:");
  display.setCursor(88, 0); display.setTextSize(2); display.print((int)hum); display.print("%");
  
  // --- Blue Zone Content (Step-by-Step Instructions) ---
  // Line 1: Action prompt
  display.setTextSize(1);
  display.setCursor(14, 24); 
  display.print("1. CONNECT TO WI-FI:");
  
  // Line 2: Hotspot SSID Name (Centered slightly)
  display.setCursor(4, 38);
  display.print(apSSID);
  
  // Line 3: The Target IP Address instruction
  display.setCursor(14, 52);
  display.print("2. GO TO: 192.168.4.1");
  
  display.display();
}

void setup() {
  Serial.begin(115200);
  while(!Serial) { 
    delay(10); 
  }
  
  Serial.println("\n--- Serial Monitor Initialized Successfully ---");

  // --- 1. HARDWARE CORE INITIALIZATION FIRST ---
  // We must wake up I2C and the display immediately so the escape hatch can use them!
  Wire.begin(I2C_SDA, I2C_SCL);
  
  if (!bme.begin(0x76, &Wire)) {
    Serial.println("[ERROR] Could not find a valid BME280 sensor, check wiring!");
  }
  
  display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS);
  display.clearDisplay();
  
  lastPressure = bme.readPressure() / 100.0F;

  Serial.println("\n--- Checking Boot Sequence Escape Hatch ---");

  // Increment our reboot counter
  bootCountSincePowerOn++;
  Serial.print("[SYSTEM] Boot sequence count: ");
  Serial.println(bootCountSincePowerOn);

  // --- 2. HARDWARE DOUBLE-RESET ESCAPE HATCH DETECTION ---
  if (bootCountSincePowerOn >= 2) {
    Serial.println("[ESCAPE HATCH ALERT] Double-reset detected! Wiping credentials...");
    
    preferences.begin("wifi-creds", false); 
    preferences.clear(); 
    preferences.end();
    
    savedSSID = ""; 
    savedPASS = "";
    tsAPIKey = SECRET_TS_KEY; // Reset to default fallback key
    
    bootCountSincePowerOn = 0; // Reset the counter
    
    // SAFE TO USE NOW: Show the confirmation on the OLED screen
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(10, 25);
    display.print("SETTINGS CLEARED!");
    display.setCursor(10, 40);
    display.print("Opening Portal...");
    display.display();
    delay(2000);
  }

  // --- 3. NETWORK STARTUP ---
  initWiFi();

  if (isConfigured) {
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

    // Wake up the Blynk background engine
    Blynk.config(blynkAuthKey.c_str());
    Blynk.connect();

    server.on("/", handleRoot);
    server.begin();
    Serial.println("[SUCCESS] HTTP Web Server Started!");
  } else {
    Serial.println("[PORTAL] Standing by for user connection at 192.168.4.1...");
  }

  // --- 4. INITIAL DATA LOGGING ---
  float currentT = bme.readTemperature();
  float currentP = bme.readPressure() / 100.0F;
  float currentH = bme.readHumidity();

  logHistoryData(currentT, currentP, currentH);
  
// Only push to cloud if we have an internet connection AND a key is actually written
  // Only push to cloud platforms if we actually have an active internet router connection
  if (isConfigured) {
    if (tsAPIKey != "") {
      updateThingSpeakCloud(currentT, currentH, currentP);
    } else {
      Serial.println("[CLOUD] Skipping initial ThingSpeak update: Key is empty.");
    }

    if (blynkAuthKey != "") {
      updateBlynkCloud(currentT, currentH, currentP);
    } else {
      Serial.println("[CLOUD] Skipping initial Blynk update: Auth Key is empty.");
    }
  } else {
    Serial.println("[CLOUD] Skipping initial cloud updates: Station running in local portal mode.");
  }

  lastHistoryLogTime = millis(); 
}

void loop() {
  server.handleClient();

  // --- ROUTE A: Normal Operational Behavior ---
  if (isConfigured) {
    Blynk.run();
    // --- Clean, single-run execution to clear escape hatch flag ---
    if (bootCountSincePowerOn > 0 && millis() > 3500) {
      bootCountSincePowerOn = 0;
      Serial.println("[SYSTEM] System stable. Escape hatch counter cleared.");
    }

    // 5-Minute Sensor Logging & Cloud Pushing Engine
    if (millis() - lastHistoryLogTime >= logInterval) {
      lastHistoryLogTime = millis();
      float currentT = bme.readTemperature();
      float currentP = bme.readPressure() / 100.0F;
      float currentH = bme.readHumidity();

      logHistoryData(currentT, currentP, currentH);

      // Explicit Cloud upload validation
      if (tsAPIKey != "") {
        updateThingSpeakCloud(currentT, currentH, currentP);
      } else {
        Serial.println("[CLOUD] Data captured locally, but skipped cloud upload (API key is empty).");
      }
      if (blynkAuthKey != "") updateBlynkCloud(currentT, currentH, currentP);

      // Execute this block inside your 5-minute timer loop!
      float currentPressure = bme.readPressure() / 100.0F; // Get hPa

      // Shift history array to make room for the new reading
      for (int i = 0; i < TREND_WINDOW_SIZE - 1; i++) {
          pressureHistory[i] = pressureHistory[i + 1];
      }
      pressureHistory[TREND_WINDOW_SIZE - 1] = currentPressure;

      if (zambrettiCount < TREND_WINDOW_SIZE) {
          zambrettiCount++;
          zambrettiForecast = "Gathering Data (" + String(zambrettiCount) + "/" + String(TREND_WINDOW_SIZE) + ")";
      } else {
          // We have a full 3 hours of data! Run the engine.
          float pressure3HoursAgo = pressureHistory[0];
          zambrettiForecast = calculateZambretti(currentPressure, pressure3HoursAgo);
      }
    }

    // 2.5-Second Main Screen Refresh & Heart Animation
    if (millis() - lastAnimationToggle >= 2500) {
      lastAnimationToggle = millis(); 
      heartIsFilled = !heartIsFilled; 

      float temp = bme.readTemperature();
      float hum = bme.readHumidity();
      float pres = bme.readPressure() / 100.0F;
      float alt = bme.readAltitude(SEALEVELPRESSURE_HPA);

      if (pres > lastPressure + 0.05F) trendArrow = "^";
      else if (pres < lastPressure - 0.05F) trendArrow = "v";
      lastPressure = pres;

      display.clearDisplay();
      display.setTextColor(SSD1306_WHITE);

      int tempWhole = (int)temp;
      int tempDec = (int)(abs(temp) * 10) % 10;

      display.setTextSize(1); display.setCursor(0, 0); display.print("T:");
      display.setCursor(12, 0); display.setTextSize(2); display.print(tempWhole);
      
      int xOffset = (tempWhole >= 10 || tempWhole <= -10) ? 36 : 24;
      
      display.setTextSize(1);
      display.setCursor(xOffset, 8); display.print(","); display.print(tempDec);
      display.setCursor(xOffset + 6, 0); display.print("C"); 

      if (heartIsFilled) {
        display.fillCircle(56, 3, 2, SSD1306_WHITE); display.fillCircle(60, 3, 2, SSD1306_WHITE);
        display.fillTriangle(54, 4, 62, 4, 58, 10, SSD1306_WHITE);
      } else {
        display.drawCircle(56, 3, 2, SSD1306_WHITE); display.drawCircle(60, 3, 2, SSD1306_WHITE);
        display.drawTriangle(54, 4, 62, 4, 58, 10, SSD1306_WHITE);
        display.drawPixel(56, 3, SSD1306_BLACK); display.drawPixel(60, 3, SSD1306_BLACK);
      }

      display.setTextSize(1); display.setCursor(76, 0); display.print("H:");
      display.setCursor(88, 0); display.setTextSize(2); display.print((int)hum); display.setTextSize(1); display.print("%");

      display.setTextSize(1); display.setCursor(2, 24); display.print(ipStr);
      display.drawLine(84, 22, 84, 32, SSD1306_WHITE);
      
      display.setCursor(90, 24);
      struct tm timeinfo;
      String timeStr = "--:--";
      if (getLocalTime(&timeinfo)) {
        char timeBuffer[6]; strftime(timeBuffer, sizeof(timeBuffer), "%H:%M", &timeinfo);
        timeStr = String(timeBuffer);
      }
      display.print(timeStr);

      display.setCursor(2, 38); display.setTextSize(2); display.print(pres, 0); 
      int hPa_X_Offset = (pres >= 1000.0F) ? 50 : 38; int arrow_X_Offset = hPa_X_Offset + 20;
      display.setTextSize(1); display.setCursor(hPa_X_Offset, 45); display.print("hPa");
      
      if (trendArrow == "^") display.drawTriangle(arrow_X_Offset, 43, arrow_X_Offset + 6, 43, arrow_X_Offset + 3, 38, SSD1306_WHITE);
      else if (trendArrow == "v") display.drawTriangle(arrow_X_Offset, 38, arrow_X_Offset + 6, 38, arrow_X_Offset + 3, 43, SSD1306_WHITE);

      display.setCursor(68, 38); display.setTextSize(2); display.print(alt, 0); display.setTextSize(1); display.print("m");
      display.setCursor(2, 54); display.print("PRESSURE");
      display.setCursor(68, 54); display.print("ALTITUDE");
      display.display();
    }
  } 
  // --- ROUTE B: Setup Portal Active Behavior ---
  else {
    if (millis() - lastAnimationToggle >= 2500) {
      lastAnimationToggle = millis();
      updateDisplayPortal();
    }
  }
}