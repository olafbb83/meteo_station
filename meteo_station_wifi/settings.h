// --- Station Local Settings ---
// Pressure offset to adjust Zambretti forecast bands for local elevation.
// Standard sea-level reference is 1013.25 hPa.
// Sabadell (~190m): typical fair-weather pressure ~997 hPa -> offset = -16.25
#define LOCAL_PRESSURE_OFFSET (-16.25)

// --- Lux thresholds for forecast cross-reference ---
// Tune these based on your observed Serial Monitor values
#define LUX_BRIGHT_SUN   10000.0   // Direct sunlight
#define LUX_OVERCAST      500.0    // Heavily overcast / indoor bright
#define LUX_NIGHT          10.0    // Night / sensor covered

// --- Humidity thresholds for forecast cross-reference ---
#define HUM_HIGH  85.0   // High humidity -> rain/fog more likely
#define HUM_LOW   40.0   // Low humidity  -> dry/clearing confirmed

// --- MQ-135 Air Quality Sensor ---
#define MQ135_PIN      3      // GPIO pin for MQ-135 AOUT
#define MQ135_RL      10.0    // Load resistance on board (kΩ)
#define MQ135_R0      9.24    // Sensor resistance in clean air (kΩ) — calibrated 2026-06-02
#define MQ135_SAMPLES 10      // ADC samples to average per reading

// --- Diurnal pressure correction table (hPa, one value per hour 0-23) ---
// Subtract from raw pressure before feeding into Zambretti.
// Based on standard atmospheric tide model for ~40°N latitude (Sabadell 41.5°N).
// Tune individual hours if you notice false alarms at specific times of day.
#define DIURNAL_CORRECTION { \
  -0.8, -0.9, -0.8, -0.6, -0.3,  0.1, \
   0.5,  0.7,  0.8,  0.9,  0.8,  0.6, \
   0.3, -0.1, -0.4, -0.6, -0.7, -0.6, \
  -0.3,  0.1,  0.4,  0.6,  0.5,  0.0  \
}
