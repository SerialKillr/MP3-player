// ============================================================
//  AD 5-Button Module — Calibration Sketch
//
//  HOW TO USE:
//    1. Wire AD module OUT pin to GPIO34, VCC to 3.3V, GND to GND
//    2. Upload this sketch to your ESP32
//    3. Open Serial Monitor → Tools → Serial Monitor
//    4. Set baud rate to 115200 (bottom right of Serial Monitor)
//    5. Press and HOLD each button one at a time for 2-3 seconds
//    6. Write down the MIN and MAX values you see per button
//    7. Open esp32_ipod.ino and replace the BTN_*_MIN/MAX values
//
//  WHAT TO WRITE DOWN:
//    For each button, note the lowest and highest number printed
//    while holding it. Add a small margin of ~50 on each side.
//
//  EXAMPLE:
//    Holding UP     → you see  10 to 25    → set MIN=0,    MAX=40
//    Holding RIGHT  → you see 370 to 410   → set MIN=350,  MAX=450
//    Holding LEFT   → you see 1080 to 1120 → set MIN=1040, MAX=1160
//    Holding DOWN   → you see 1770 to 1850 → set MIN=1730, MAX=1890
//    Holding CENTER → you see 2660 to 2740 → set MIN=2630, MAX=2770
//    Nothing pressed→ you see 3300+        → this is NONE, ignore
//
//  IMPORTANT:
//    Make sure no two buttons have overlapping MIN/MAX ranges.
//    If two ranges are too close, widen the margin on the
//    lower button's MAX downward rather than upward.
// ============================================================

#define AD_PIN 34

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("=========================================");
  Serial.println("  AD 5-Button Module Calibration Sketch  ");
  Serial.println("=========================================");
  Serial.println("Press and hold each button one at a time.");
  Serial.println("Nothing pressed will show 3000+");
  Serial.println("-----------------------------------------");
}

void loop() {
  int val = analogRead(AD_PIN);

  Serial.print("Raw value: ");
  Serial.print(val);

  delay(2000); 
 


}
