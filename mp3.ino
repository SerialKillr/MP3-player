// ============================================================
//  ESP32 iPod — Full Firmware
//  Components:
//    - ESP32 DevKit (base, no PSRAM)
//    - MP3-TF-16P V3.0 audio decoder (UART)
//    - SSD1306 0.96" OLED (I2C)
//    - AD 5-button keyboard module (ADC)
//    - 2x tactile side buttons for volume (GPIO)
//
//  Libraries required (install via Arduino Library Manager):
//    - DFRobotDFPlayerMini  by DFRobot
//    - Adafruit SSD1306      by Adafruit
//    - Adafruit GFX Library  by Adafruit
//
//  Pin wiring:
//    OLED SDA  → GPIO21
//    OLED SCL  → GPIO22
//    MP3 TX    → GPIO16  (ESP32 RX2)
//    MP3 RX    → GPIO17  (ESP32 TX2) via 1kΩ resistor
//    AD module → GPIO34  (ADC1_CH6)
//    Vol+      → GPIO27  + GND
//    Vol−      → GPIO14  + GND
//
//  ⚠ NOTE ON SEEKING:
//    The MP3-TF-16P / DFPlayer Mini protocol does NOT support
//    seeking to an arbitrary timestamp within a track.
//    Left/Right buttons display the seek offset visually but
//    do not actually jump to that position in the audio.
//    True seeking would require a different audio module.
// ============================================================

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DFRobotDFPlayerMini.h>
#include <HardwareSerial.h>

// ── OLED ─────────────────────────────────────────────────────
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT  64
#define OLED_RESET     -1
#define OLED_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ── MP3 player ───────────────────────────────────────────────
HardwareSerial mp3Serial(2);        // UART2 (GPIO16 RX, GPIO17 TX)
DFRobotDFPlayerMini mp3;

// ── Pin definitions ──────────────────────────────────────────
#define AD_PIN        34            // AD keyboard module analog output
#define VOL_UP_PIN    27            // Side button +
#define VOL_DOWN_PIN  14            // Side button −

// ── ADC thresholds for AD keyboard ───────────────────────────
//  Supply: 3.3V  →  ADC full scale ≈ 4095
//  These are approximate — run CALIBRATE_MODE first if buttons
//  feel wrong, then update these values.
//
//  To calibrate: set CALIBRATE_MODE to true below, open Serial
//  Monitor at 115200 baud, press each button, note the readings,
//  and update the MIN/MAX pairs accordingly.
#define CALIBRATE_MODE false

#define BTN_UP_MIN       0
#define BTN_UP_MAX       40      // UP measured at 0
#define BTN_RIGHT_MIN    350
#define BTN_RIGHT_MAX    450     // RIGHT measured 370-410
#define BTN_LEFT_MIN     1040
#define BTN_LEFT_MAX     1160    // LEFT measured at 1100
#define BTN_DOWN_MIN     1730
#define BTN_DOWN_MAX     1890    // DOWN measured 1770-1850
#define BTN_CENTER_MIN   2630
#define BTN_CENTER_MAX   2770    // SELECT measured at 2700

// Anything above BTN_UP_MAX = no button pressed

// ── Button identifiers ───────────────────────────────────────
enum Button { NONE, BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_CENTER };

// ── Timing constants ─────────────────────────────────────────
#define DEBOUNCE_MS       200       // ignore re-presses within this window
#define VOL_REPEAT_MS      80       // how fast volume changes while held
#define DISPLAY_SCROLL_MS 300       // marquee scroll speed (ms per step)

// ── State ────────────────────────────────────────────────────
int  volume       = 15;            // 0 – 30
int  totalSongs   = 0;
int  currentSong  = 1;
bool isPlaying    = false;

unsigned long elapsedSeconds  = 0; // display timer (not hardware seek)
unsigned long lastTickTime    = 0; // tracks 1-second display ticks
unsigned long lastButtonTime  = 0; // debounce timestamp
unsigned long lastVolTime     = 0; // volume repeat timestamp

// ── Marquee (scrolling song title) ───────────────────────────
String  songTitle      = "";
int     marqueeOffset  = 0;
unsigned long lastMarqueeTime = 0;

// ── Forward declarations ──────────────────────────────────────
Button  readADButton();
void    playSong(int number);
void    updateDisplay();
void    showSplash();
void    showError(const char* msg);
String  formatTime(unsigned long secs);

// =============================================================
//  SETUP
// =============================================================
void setup() {
  Serial.begin(115200);

  // ── OLED ───────────────────────────────────────────────
  Wire.begin(21, 22);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    // Can't display error — just halt
    while (true) delay(1000);
  }
  showSplash();

  // ── MP3 player ─────────────────────────────────────────
  mp3Serial.begin(9600, SERIAL_8N1, 16, 17);
  delay(1000);
  if (!mp3.begin(mp3Serial, true, false)) {
    showError("MP3 init failed\nCheck wiring");
    while (true) delay(1000);
  }

  delay(200);
  mp3.volume(volume);
  delay(200);

  totalSongs = mp3.readFileCounts();
  if (totalSongs <= 0) {
    showError("No files found\nCheck SD card");
    while (true) delay(1000);
  }

  // ── Button pins ────────────────────────────────────────
  pinMode(VOL_UP_PIN,   INPUT_PULLUP);
  pinMode(VOL_DOWN_PIN, INPUT_PULLUP);
  // AD_PIN is input-only (GPIO34), no pinMode needed for ADC

  // ── Start playing ──────────────────────────────────────
  playSong(1);
}

// =============================================================
//  MAIN LOOP
// =============================================================
void loop() {
  unsigned long now = millis();

  // ── Calibration mode — print raw ADC value to Serial ──
  #if CALIBRATE_MODE
    Serial.println(analogRead(AD_PIN));
    delay(100);
    return;
  #endif

  // ── Elapsed time tick (1 second) ──────────────────────
  if (isPlaying && now - lastTickTime >= 1000) {
    elapsedSeconds++;
    lastTickTime = now;
    updateDisplay();
  }

  // ── Marquee scroll ────────────────────────────────────
  if (songTitle.length() > 16 && now - lastMarqueeTime > DISPLAY_SCROLL_MS) {
    marqueeOffset++;
    if (marqueeOffset > (int)songTitle.length()) marqueeOffset = 0;
    lastMarqueeTime = now;
    updateDisplay();
  }

  // ── DFPlayer events (song finished) ───────────────────
  if (mp3.available()) {
    uint8_t type = mp3.readType();
    if (type == DFPlayerPlayFinished) {
      int next = currentSong + 1;
      if (next > totalSongs) next = 1;
      playSong(next);
    }
  }

  // ── Volume side buttons (with hold-to-repeat) ─────────
  bool volUpHeld   = digitalRead(VOL_UP_PIN)   == LOW;
  bool volDownHeld = digitalRead(VOL_DOWN_PIN) == LOW;

  if ((volUpHeld || volDownHeld) && now - lastVolTime > VOL_REPEAT_MS) {
    if (volUpHeld) {
      volume = min(30, volume + 1);
    } else {
      volume = max(0, volume - 1);
    }
    mp3.volume(volume);
    lastVolTime = now;
    updateDisplay();
  }

  // ── AD keyboard ───────────────────────────────────────
  Button btn = readADButton();
  if (btn != NONE && now - lastButtonTime > DEBOUNCE_MS) {
    lastButtonTime = now;

    switch (btn) {

      case BTN_UP:      // ▲ Previous song
        {
          int prev = currentSong - 1;
          if (prev < 1) prev = totalSongs;
          playSong(prev);
        }
        break;

      case BTN_DOWN:    // ▼ Next song
        {
          int next = currentSong + 1;
          if (next > totalSongs) next = 1;
          playSong(next);
        }
        break;

      case BTN_LEFT:    // ◀ Seek −10 sec (display only)
        if (elapsedSeconds >= 10) elapsedSeconds -= 10;
        else                      elapsedSeconds  = 0;
        updateDisplay();
        break;

      case BTN_RIGHT:   // ▶ Seek +10 sec (display only)
        elapsedSeconds += 10;
        updateDisplay();
        break;

      case BTN_CENTER:  // ● Play / Pause
        if (isPlaying) {
          mp3.pause();
          isPlaying = false;
        } else {
          mp3.start();
          isPlaying = true;
        }
        updateDisplay();
        break;

      default:
        break;
    }
  }
}

// =============================================================
//  PLAY A SONG
// =============================================================
void playSong(int number) {
  currentSong    = number;
  elapsedSeconds = 0;
  marqueeOffset  = 0;
  lastTickTime   = millis();

  mp3.play(currentSong);
  isPlaying = true;

  // Build a display title — DFPlayer doesn't return file names
  // so we show the track number. Swap in real names if you want
  // by populating a lookup array in your own code.
  songTitle = "Track " + String(currentSong);

  updateDisplay();
}

// =============================================================
//  READ AD KEYBOARD
// This calibration has to be done manually and changes from keyboard to keyboard therefore I have attached another piece of .ino code that'll help determine min & max for each 
// =============================================================
Button readADButton() {
  int val = analogRead(AD_PIN);
  if (val <= BTN_CENTER_MAX)                           return BTN_CENTER;
  if (val >= BTN_RIGHT_MIN && val <= BTN_RIGHT_MAX)    return BTN_RIGHT;
  if (val >= BTN_LEFT_MIN  && val <= BTN_LEFT_MAX)     return BTN_LEFT;
  if (val >= BTN_DOWN_MIN  && val <= BTN_DOWN_MAX)     return BTN_DOWN;
  if (val >= BTN_UP_MIN    && val <= BTN_UP_MAX)       return BTN_UP;
  return NONE;
}

// =============================================================
//  FORMAT TIME  mm:ss
// =============================================================
String formatTime(unsigned long secs) {
  unsigned long m = secs / 60;
  unsigned long s = secs % 60;
  String out = "";
  if (m < 10) out += "0";
  out += String(m) + ":";
  if (s < 10) out += "0";
  out += String(s);
  return out;
}

// =============================================================
//  UPDATE OLED DISPLAY
// =============================================================
//
//  Layout (128×64):
//  ┌────────────────────────┐  y=0
//  │  ♪ ESP32 iPod          │  title bar (inverted), h=13
//  ├────────────────────────┤  y=13
//  │ Track 3 / 24           │  song number,           y=15
//  │ Blinding Lights...     │  scrolling title,       y=25
//  │ > Playing   00:47      │  status + time,         y=35
//  │ VOL [████████░░] 15    │  volume bar,            y=48
//  └────────────────────────┘  y=63
//
void updateDisplay() {
  display.clearDisplay();

  // ── Title bar (inverted) ───────────────────────────────
  display.fillRect(0, 0, 128, 13, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setTextSize(1);
  display.setCursor(4, 3);
  display.print("\x0E ESP32 iPod");       // \x0E = music note glyph

  display.setTextColor(SSD1306_WHITE);

  // ── Song number ───────────────────────────────────────
  display.setCursor(0, 15);
  display.print("Track ");
  display.print(currentSong);
  display.print(" / ");
  display.print(totalSongs);

  // ── Scrolling song title ──────────────────────────────
  display.setCursor(0, 25);
  if (songTitle.length() <= 16) {
    display.print(songTitle);
  } else {
    // Marquee: show a 16-char window sliding through the title
    String padded = songTitle + "   ";
    int len = padded.length();
    String visible = "";
    for (int i = 0; i < 16; i++) {
      visible += padded[(marqueeOffset + i) % len];
    }
    display.print(visible);
  }

  // ── Status + elapsed time ─────────────────────────────
  display.setCursor(0, 35);
  display.print(isPlaying ? "> " : "||");
  display.setCursor(14, 35);
  display.print(isPlaying ? "Playing " : "Paused  ");
  display.print(formatTime(elapsedSeconds));

  // ── Volume bar ────────────────────────────────────────
  display.setCursor(0, 50);
  display.print("VOL");
  // Outer border
  display.drawRect(22, 50, 90, 8, SSD1306_WHITE);
  // Filled portion
  int filled = map(volume, 0, 30, 0, 88);
  display.fillRect(23, 51, filled, 6, SSD1306_WHITE);
  // Numeric value
  display.setCursor(115, 50);
  if (volume < 10) display.print(" ");
  display.print(volume);

  display.display();
}

// =============================================================
//  SPLASH SCREEN
// =============================================================
void showSplash() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(14, 10);
  display.print("ESP32");
  display.setTextSize(1);
  display.setCursor(30, 34);
  display.print("iPod v1.0");
  display.setCursor(18, 48);
  display.print("by SerialKillr :)");
  display.display();
  delay(2000);
}

// =============================================================
//  ERROR SCREEN
// =============================================================
void showError(const char* msg) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 10);
  display.print("ERROR:");
  display.setCursor(0, 24);
  display.print(msg);
  display.display();
}
// Obi-wan Kenobi: hello there