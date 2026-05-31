#include <WiFi.h>
#include <WebServer.h>
#include <time.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Preferences.h>

// --- Default fallback configuration credentials ---
// Leave blank to test your portal setup configuration cleanly!
const char* defaultSSID = ""; 
const char* defaultPASS = "";

#define I2C_SDA 8
#define I2C_SCL 9
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define SCREEN_ADDRESS 0x3C
#define SEALEVELPRESSURE_HPA (1013.25)
#define MAX_HISTORY_POINTS 144

// --- Automatic Wi-Fi Onboarding Configuration ---
bool isConfigured = false;
unsigned long portalStartTime = 0;
const char* apSSID = "Meteo-Station-Setup";

Preferences preferences;
String savedSSID = "";
String savedPASS = "";

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

void handlePortalRoot() {
  String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<style>body{font-family:sans-serif; background:#0f172a; color:#f8fafc; padding:20px; text-align:center;}";
  html += "input{width:100%; max-width:300px; padding:12px; margin:8px 0; border-radius:8px; border:1px solid #334155; background:#1e293b; color:#fff; box-sizing:border-box;}";
  html += "button{background:#38bdf8; color:#0f172a; font-weight:bold; border:none; padding:12px 30px; border-radius:8px; margin-top:10px; cursor:pointer;}</style></head><body>";
  html += "<h2>Meteo Station Setup</h2><p>Enter your home Wi-Fi details below:</p>";
  html += "<form action='/save' method='POST'>";
  html += "<input type='text' name='ssid' placeholder='Wi-Fi Network Name (SSID)' required><br>";
  html += "<input type='password' name='pass' placeholder='Wi-Fi Password'><br>";
  html += "<button type='submit'>Connect Station</button></form></body></html>";
  server.send(200, "text/html", html);
}

void handlePortalSave() {
  if (server.hasArg("ssid")) {
    String reqSSID = server.arg("ssid");
    String reqPASS = server.arg("pass");

    preferences.begin("wifi-creds", false);
    preferences.putString("ssid", reqSSID);
    preferences.putString("password", reqPASS);
    preferences.end();

    String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    html += "<style>body{font-family:sans-serif; background:#0f172a; color:#f8fafc; padding:20px; text-align:center;}</style></head><body>";
    html += "<h2>Credentials Saved!</h2><p>The Meteo Station is now restarting to join your network.</p></body></html>";
    server.send(200, "text/html", html);
    
    delay(2000);
    ESP.restart();
  }
}

void initWiFi() {
  preferences.begin("wifi-creds", false);
  savedSSID = preferences.getString("ssid", "");
  savedPASS = preferences.getString("password", "");
  
  if (savedSSID == "" && String(defaultSSID) != "") {
    preferences.putString("ssid", defaultSSID);
    preferences.putString("password", defaultPASS);
    savedSSID = defaultSSID;
    savedPASS = defaultPASS;
  }
  preferences.end();
  
  // --- FIX: CRASH PROTECTION FOR EMPTY TESTING STRINGS ---
  // If there are absolutely no saved or default credentials, DO NOT call WiFi.begin()
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

  // --- AP FALLBACK ACTIVATION ---
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
  
  display.setTextSize(1); display.setCursor(0, 0); display.print("T:");
  display.setCursor(12, 0); display.setTextSize(2); display.print(tempWhole);
  
  int xOffset = (tempWhole >= 10 || tempWhole <= -10) ? 36 : 24;
  
  display.setTextSize(1);
  display.setCursor(xOffset, 8); display.print(","); display.print(tempDec);
  display.setCursor(xOffset + 6, 0); display.print("C"); 
  
  display.setTextSize(1); display.setCursor(76, 0); display.print("H:");
  display.setCursor(88, 0); display.setTextSize(2); display.print((int)hum); display.print("%");
  
  display.setTextSize(1);
  display.setCursor(14, 30);
  display.print("CONNECT TO WI-FI:");
  
  display.setCursor(4, 48);
  display.print(apSSID);
  display.display();
}

void setup() {
  Serial.begin(115200);
  while(!Serial) { 
    delay(10); 
  }
  
  Serial.println("\n--- Serial Monitor Initialized Successfully ---");

  Wire.begin(I2C_SDA, I2C_SCL);
  if (!bme.begin(0x76, &Wire)) {
    Serial.println("[ERROR] Could not find a valid BME280 sensor, check wiring!");
  }
  
  display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS);
  display.clearDisplay();
  
  lastPressure = bme.readPressure() / 100.0F;

  // ==========================================
  // --- SAFE PORTAL TESTING FORCE BLOCK ---
  // ==========================================
  preferences.begin("wifi-creds", false); 
  preferences.clear(); 
  preferences.end();
  savedSSID = ""; 
  savedPASS = "";
  // ==========================================

  initWiFi();

  if (isConfigured) {
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    server.on("/", handleRoot);
    server.begin();
    Serial.println("[SUCCESS] HTTP Web Server Started!");
  } else {
    Serial.println("[PORTAL] Standing by for user connection at 192.168.4.1...");
  }

  logHistoryData(bme.readTemperature(), (bme.readPressure() / 100.0F), bme.readHumidity());
  lastHistoryLogTime = millis(); 
}

void loop() {
  server.handleClient(); 

  // --- ROUTE A: Normal Operational Behavior ---
  if (isConfigured) {
    if (millis() - lastHistoryLogTime >= logInterval) {
      lastHistoryLogTime = millis();
      logHistoryData(bme.readTemperature(), (bme.readPressure() / 100.0F), bme.readHumidity());
    }

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