// ============================================================
//  ESP32 iPod — Full Firmware v2.0
//
//  NEW IN v2.0:
//    - SPI SD card + ESP32 built-in DAC  (WAV files)
//    - Real seeking — left/right actually jump in the audio
//    - Shuffle mode — randomised playback order
//    - Resume from last song + position on power-on
//    - Bluetooth A2DP — long-press centre to toggle wired/BT
//    - Screen auto-dim after 10 s idle
//    - Screen off after 30 s idle — any button to wake
//    - Song names loaded automatically from SD card
//
//  Libraries required  (Arduino Library Manager):
//    - ESP32-audioI2S   by schreibfaul1
//    - ESP32-A2DP       by pschatzmann
//    - Adafruit SSD1306 by Adafruit
//    - Adafruit GFX     by Adafruit
//    SD, SPI, Preferences — built into ESP32 Arduino core
//
//  Pin wiring:
//    OLED SDA  → GPIO21       SD MOSI → GPIO23
//    OLED SCL  → GPIO22       SD MISO → GPIO19
//    Audio L   → GPIO25       SD SCK  → GPIO18
//    Audio R   → GPIO26       SD CS   → GPIO5
//    AD module → GPIO34
//    Vol+      → GPIO27       Vol−    → GPIO14
//
//  SD card setup:
//    Place WAV files in the ROOT directory (not in subfolders).
//    Name them with a number first so they load in order:
//      01 Song Name.wav, 02 Another Song.wav …
//    Recommended format: 44100 Hz · 16-bit · stereo.
//    If your WAV files use a different format, update
//    WAV_BPS below accordingly.
//
//  Bluetooth:
//    Change BLUETOOTH_TARGET to your headphones' exact BT name.
//    Long-press centre (~1 second) to toggle wired ↔ BT.
//    The ESP32 connects to BLUETOOTH_TARGET automatically.
//    Once paired it reconnects on every subsequent BT mode entry.
//
//  ⚠  Built-in DAC note:
//    GPIO25 and GPIO26 are the fixed DAC pins on ESP32.
//    I2S_DOUT is set to 25; I2S_BCLK and I2S_LRC are unused
//    in internal-DAC mode and left as 0.
//    If you get no audio, swap I2S_DOUT to 26 and try again.
// ============================================================

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <SD.h>
#include <SPI.h>
#include <Preferences.h>
#include "Audio.h"
#include "BluetoothA2DPSource.h"

// ── Change this to your Bluetooth headphones' exact name ─────
#define BLUETOOTH_TARGET  "My Headphones"

// ── OLED ─────────────────────────────────────────────────────
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT  64
#define OLED_RESET     -1
#define OLED_ADDR    0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ── SD card ──────────────────────────────────────────────────
#define SD_CS    5
#define SD_MOSI 23
#define SD_MISO 19
#define SD_SCK  18

// ── Audio — built-in DAC ─────────────────────────────────────
Audio audio;
#define I2S_DOUT  25   // GPIO25 = DAC_L (left channel)
#define I2S_BCLK   0   // unused in internal-DAC mode
#define I2S_LRC    0   // unused in internal-DAC mode

// ── Bluetooth ────────────────────────────────────────────────
BluetoothA2DPSource a2dp_source;
volatile bool btConnected = false;
volatile bool btSongEnded = false;
File          btFile;              // open WAV file used in BT mode

// ── Button pins ──────────────────────────────────────────────
#define AD_PIN        34
#define VOL_UP_PIN    27
#define VOL_DOWN_PIN  14

// ── ADC calibration ──────────────────────────────────────────
//  Set CALIBRATE_MODE to true, open Serial Monitor at 115200,
//  press each button and note the values printed, then update
//  the MIN/MAX pairs below. Set CALIBRATE_MODE back to false.
#define CALIBRATE_MODE false

#define BTN_UP_MIN       0
#define BTN_UP_MAX      40
#define BTN_RIGHT_MIN  350
#define BTN_RIGHT_MAX  450
#define BTN_LEFT_MIN  1040
#define BTN_LEFT_MAX  1160
#define BTN_DOWN_MIN  1730
#define BTN_DOWN_MAX  1890
#define BTN_CENTER_MIN 2630
#define BTN_CENTER_MAX 2770

enum Button { NONE, BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_CENTER };

// ── Output mode ──────────────────────────────────────────────
enum OutputMode { MODE_WIRED, MODE_BT };
OutputMode outputMode = MODE_WIRED;

// ── Timing ───────────────────────────────────────────────────
#define DEBOUNCE_MS       200
#define VOL_REPEAT_MS      80
#define SCROLL_MS         300
#define SAVE_INTERVAL_MS 5000   // save state to flash every 5 s
#define DIM_TIMEOUT_MS  10000   // dim screen after 10 s idle
#define SLEEP_TIMEOUT_MS 30000  // screen off after 30 s idle
#define LONG_PRESS_MS     800   // centre long-press threshold

// ── WAV format ───────────────────────────────────────────────
//  44100 Hz · 16-bit · stereo = 176 400 bytes per second.
//  Change WAV_BPS if your files use a different format.
#define WAV_BPS  176400UL
#define WAV_HDR       44        // standard WAV header size

// ── Playback state ───────────────────────────────────────────
int  volume       = 15;         // 0 – 30
int  totalSongs   = 0;
int  currentSong  = 1;
bool isPlaying    = false;
bool shuffleMode  = false;

// ── Display state ────────────────────────────────────────────
bool displayAwake  = true;
bool displayDimmed = false;

// ── Timestamps ───────────────────────────────────────────────
unsigned long elapsedSeconds   = 0;
unsigned long lastTickTime     = 0;
unsigned long lastButtonTime   = 0;
unsigned long lastVolTime      = 0;
unsigned long lastSaveTime     = 0;
unsigned long lastActivityTime = 0;

// ── Long-press tracking ──────────────────────────────────────
unsigned long centerPressTime  = 0;
bool          centerHeld       = false;
bool          longPressHandled = false;

// ── Resume-seek ──────────────────────────────────────────────
bool          seekOnResume     = false;
uint32_t      seekOnResumePos  = 0;
unsigned long resumeElapsed    = 0;

// ── Song data ────────────────────────────────────────────────
#define MAX_SONGS 100
String songNames[MAX_SONGS];   // display name  (no .wav)
String songPaths[MAX_SONGS];   // full SD path  e.g. "/01 Song.wav"

// ── Shuffle ──────────────────────────────────────────────────
int shuffleOrder[MAX_SONGS];
int shuffleIndex = 0;

// ── Marquee ──────────────────────────────────────────────────
String        songTitle    = "";
int           marqueeOff   = 0;
unsigned long lastScrollMs = 0;

// ── Persistent storage ───────────────────────────────────────
Preferences prefs;

// ── Forward declarations ─────────────────────────────────────
Button readADButton();
void   playSong(int n);
void   seekRelative(int secs);
void   loadSongList();
void   generateShuffleOrder();
int    songNext();
int    songPrev();
void   saveState();
void   loadState();
void   activityDetected();
void   handleDisplayPower(unsigned long now);
void   dimDisplay();
void   wakeDisplay();
void   sleepDisplay();
void   switchToWired();
void   switchToBT();
void   updateDisplay();
void   showSplash();
void   showError(const char *msg);
String formatTime(unsigned long s);

// =============================================================
//  BLUETOOTH CALLBACKS
//  btAudioData runs inside the BT FreeRTOS task.
//  It reads raw PCM samples from the open WAV file and
//  hands them to the A2DP stack.
// =============================================================
int32_t btAudioData(Frame *frame, int32_t frame_count) {
  if (!btFile || !isPlaying) {
    memset(frame, 0, frame_count * sizeof(Frame));
    return frame_count;
  }
  if (btFile.read((uint8_t *)frame, frame_count * sizeof(Frame)) <= 0)
    btSongEnded = true;   // signal main loop to advance track
  return frame_count;
}

void btConnectionState(esp_a2d_connection_state_t state, void *) {
  btConnected = (state == ESP_A2D_CONNECTION_STATE_CONNECTED);
}

// =============================================================
//  WIRED AUDIO CALLBACKS
//  Called automatically by the ESP32-audioI2S library.
// =============================================================
void audio_eof_mp3(const char *) {
  // Fires at end of every WAV (and MP3) file
  playSong(songNext());
}

void audio_info(const char *info) {
  #if CALIBRATE_MODE
    Serial.println(info);
  #endif
}

// =============================================================
//  SETUP
// =============================================================
void setup() {
  Serial.begin(115200);

  // ── OLED ───────────────────────────────────────────────
  Wire.begin(21, 22);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    while (true) delay(1000);   // halt — nothing else to do
  }
  showSplash();

  // ── SD card ────────────────────────────────────────────
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS)) {
    showError("SD failed\nCheck wiring");
    while (true) delay(1000);
  }

  loadSongList();
  if (totalSongs == 0) {
    showError("No WAV files\non SD card");
    while (true) delay(1000);
  }

  // ── Audio (wired) ──────────────────────────────────────
  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
  audio.setVolume(map(volume, 0, 30, 0, 21));

  // ── Buttons ────────────────────────────────────────────
  pinMode(VOL_UP_PIN,   INPUT_PULLUP);
  pinMode(VOL_DOWN_PIN, INPUT_PULLUP);

  // ── Shuffle ────────────────────────────────────────────
  randomSeed(analogRead(33));   // floating pin gives noisy seed
  generateShuffleOrder();

  // ── Load last session from NVS flash ───────────────────
  loadState();

  lastActivityTime = millis();
  lastTickTime     = millis();
  lastSaveTime     = millis();

  playSong(currentSong);
  // If resume seek was queued by loadState(), it fires in loop
}

// =============================================================
//  MAIN LOOP
// =============================================================
void loop() {
  unsigned long now = millis();

  // ── Calibration — print raw ADC to Serial ─────────────
  #if CALIBRATE_MODE
    Serial.println(analogRead(AD_PIN));
    delay(100);
    return;
  #endif

  // ── Audio library tick (must run every loop in wired mode)
  if (outputMode == MODE_WIRED) {
    audio.loop();

    // Resume seek: wait until audio is running, then jump
    if (seekOnResume && audio.isRunning()) {
      audio.setFilePos(seekOnResumePos);
      elapsedSeconds = resumeElapsed;
      seekOnResume   = false;
    }
  }

  // ── BT end-of-song (flag set in BT task, handled here) ─
  if (outputMode == MODE_BT && btSongEnded) {
    btSongEnded = false;
    playSong(songNext());
  }

  // ── Display power management ───────────────────────────
  handleDisplayPower(now);

  // ── Elapsed time tick (1 second) ──────────────────────
  if (isPlaying && now - lastTickTime >= 1000) {
    elapsedSeconds++;
    lastTickTime = now;
    updateDisplay();
  }

  // ── Marquee scroll ────────────────────────────────────
  if (displayAwake && !displayDimmed &&
      (int)songTitle.length() > 16 &&
      now - lastScrollMs > SCROLL_MS) {
    marqueeOff++;
    if (marqueeOff > (int)songTitle.length()) marqueeOff = 0;
    lastScrollMs = now;
    updateDisplay();
  }

  // ── Auto-save state every 5 seconds ───────────────────
  if (now - lastSaveTime > SAVE_INTERVAL_MS) {
    saveState();
    lastSaveTime = now;
  }

  // ── Volume side buttons (hold to repeat) ──────────────
  bool volUp   = digitalRead(VOL_UP_PIN)   == LOW;
  bool volDown = digitalRead(VOL_DOWN_PIN) == LOW;

  if ((volUp || volDown) && now - lastVolTime > VOL_REPEAT_MS) {
    lastVolTime = now;
    activityDetected();
    if (!displayAwake) { wakeDisplay(); return; }
    if (volUp)   volume = min(30, volume + 1);
    else         volume = max(0,  volume - 1);
    if (outputMode == MODE_WIRED)
      audio.setVolume(map(volume, 0, 30, 0, 21));
    updateDisplay();
  }

  // ── AD keyboard ───────────────────────────────────────
  Button btn = readADButton();

  // Centre long-press tracking
  if (btn == BTN_CENTER && !centerHeld) {
    centerHeld       = true;
    longPressHandled = false;
    centerPressTime  = now;
  }
  if (btn != BTN_CENTER) centerHeld = false;

  // Fire long press once threshold is crossed
  if (centerHeld && !longPressHandled &&
      now - centerPressTime > LONG_PRESS_MS) {
    longPressHandled = true;
    lastButtonTime   = now;
    activityDetected();
    if (!displayAwake) { wakeDisplay(); return; }
    // Toggle output mode
    (outputMode == MODE_WIRED) ? switchToBT() : switchToWired();
    return;
  }

  // Short press
  if (btn != NONE && !longPressHandled &&
      now - lastButtonTime > DEBOUNCE_MS) {
    lastButtonTime = now;
    activityDetected();

    // First press only wakes the screen — does not trigger action
    if (!displayAwake) { wakeDisplay(); return; }

    switch (btn) {

      case BTN_UP:                          // ▲ Previous song
        playSong(songPrev());
        break;

      case BTN_DOWN:                        // ▼ Next song
        playSong(songNext());
        break;

      case BTN_LEFT:                        // ◀ Seek −10 sec
        seekRelative(-10);
        break;

      case BTN_RIGHT:                       // ▶ Seek +10 sec
        seekRelative(+10);
        break;

      case BTN_CENTER:                      // ● Play / Pause
        if (isPlaying) {
          if (outputMode == MODE_WIRED) audio.pauseResume();
          isPlaying = false;
        } else {
          if (outputMode == MODE_WIRED) audio.pauseResume();
          isPlaying = true;
        }
        updateDisplay();
        break;

      default: break;
    }
  }
}

// =============================================================
//  LOAD SONG LIST FROM SD CARD
//  Scans the root directory once at boot and fills songNames[]
//  and songPaths[] with every .wav file found.
// =============================================================
void loadSongList() {
  totalSongs = 0;
  File root  = SD.open("/");
  if (!root) return;

  while (totalSongs < MAX_SONGS) {
    File f = root.openNextFile();
    if (!f) break;

    if (!f.isDirectory()) {
      String name  = String(f.name());
      String upper = name;
      upper.toUpperCase();
      if (upper.endsWith(".WAV")) {
        songPaths[totalSongs] = "/" + name;
        // Strip extension for display
        songNames[totalSongs] = name.substring(0, name.length() - 4);
        totalSongs++;
      }
    }
    f.close();
  }
  root.close();
}

// =============================================================
//  SHUFFLE ORDER — Fisher-Yates
// =============================================================
void generateShuffleOrder() {
  for (int i = 0; i < MAX_SONGS; i++) shuffleOrder[i] = i + 1;
  for (int i = totalSongs - 1; i > 0; i--) {
    int j = random(0, i + 1);
    int t = shuffleOrder[i];
    shuffleOrder[i] = shuffleOrder[j];
    shuffleOrder[j] = t;
  }
  shuffleIndex = 0;
}

int songNext() {
  if (shuffleMode) {
    shuffleIndex = (shuffleIndex + 1) % totalSongs;
    return shuffleOrder[shuffleIndex];
  }
  return (currentSong % totalSongs) + 1;
}

int songPrev() {
  if (shuffleMode) {
    shuffleIndex = (shuffleIndex - 1 + totalSongs) % totalSongs;
    return shuffleOrder[shuffleIndex];
  }
  return (currentSong - 2 + totalSongs) % totalSongs + 1;
}

// =============================================================
//  PLAY A SONG
// =============================================================
void playSong(int n) {
  if (n < 1 || n > totalSongs) n = 1;
  currentSong    = n;
  elapsedSeconds = 0;
  marqueeOff     = 0;
  lastTickTime   = millis();
  songTitle      = songNames[currentSong - 1];

  if (outputMode == MODE_WIRED) {
    audio.connecttoFS(SD, songPaths[currentSong - 1].c_str());
  } else {
    // BT mode — open file; btAudioData() reads from here
    if (btFile) btFile.close();
    btFile = SD.open(songPaths[currentSong - 1].c_str());
    if (btFile) btFile.seek(WAV_HDR);  // skip WAV header
  }

  isPlaying = true;
  updateDisplay();
}

// =============================================================
//  SEEK RELATIVE — real hardware seek
//  Works in both wired and BT modes.
// =============================================================
void seekRelative(int secs) {
  long shift = (long)secs * (long)WAV_BPS;

  if (outputMode == MODE_WIRED) {
    long newPos = (long)audio.getFilePos() + shift;
    if (newPos < WAV_HDR) newPos = WAV_HDR;
    audio.setFilePos((uint32_t)newPos);
  } else if (btFile) {
    long newPos = (long)btFile.position() + shift;
    if (newPos < WAV_HDR) newPos = WAV_HDR;
    btFile.seek((uint32_t)newPos);
  }

  long newSecs = (long)elapsedSeconds + secs;
  elapsedSeconds = (newSecs < 0) ? 0 : (unsigned long)newSecs;
  updateDisplay();
}

// =============================================================
//  MODE SWITCHING
// =============================================================
void switchToWired() {
  outputMode = MODE_WIRED;
  a2dp_source.end();
  if (btFile) btFile.close();
  delay(300);
  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
  audio.setVolume(map(volume, 0, 30, 0, 21));
  playSong(currentSong);
  updateDisplay();
}

void switchToBT() {
  outputMode = MODE_BT;
  audio.stopSong();
  delay(300);
  a2dp_source.set_on_connection_state_changed(btConnectionState);
  a2dp_source.start(BLUETOOTH_TARGET, btAudioData);
  playSong(currentSong);
  updateDisplay();
}

// =============================================================
//  SAVE / LOAD STATE
//  Uses NVS flash (Preferences) — survives power cuts.
//  Saved automatically every 5 seconds while playing.
// =============================================================
void saveState() {
  prefs.begin("ipod", false);
  prefs.putInt("song",     currentSong);
  prefs.putULong("secs",   elapsedSeconds);
  prefs.putInt("vol",      volume);
  prefs.putBool("shuffle", shuffleMode);
  prefs.end();
}

void loadState() {
  prefs.begin("ipod", true);   // read-only
  int s = prefs.getInt("song", 1);
  if (s >= 1 && s <= totalSongs) currentSong = s;

  unsigned long savedSecs = prefs.getULong("secs", 0);
  int v = prefs.getInt("vol", 15);
  if (v >= 0 && v <= 30) volume = v;
  shuffleMode = prefs.getBool("shuffle", false);
  prefs.end();

  // Queue a seek so audio resumes from the saved position
  if (savedSecs > 0) {
    seekOnResume    = true;
    seekOnResumePos = WAV_HDR + (uint32_t)(savedSecs * WAV_BPS);
    resumeElapsed   = savedSecs;
  }
}

// =============================================================
//  DISPLAY POWER MANAGEMENT
//  Idle < 10 s  → full brightness
//  Idle 10–30 s → dimmed
//  Idle > 30 s  → display off
//  Any button   → wake (first press only wakes, does not act)
// =============================================================
void activityDetected() {
  lastActivityTime = millis();
}

void handleDisplayPower(unsigned long now) {
  unsigned long idle = now - lastActivityTime;
  if (displayAwake && !displayDimmed && idle > DIM_TIMEOUT_MS)
    dimDisplay();
  else if (displayDimmed && idle > SLEEP_TIMEOUT_MS)
    sleepDisplay();
}

void dimDisplay() {
  display.ssd1306_command(SSD1306_SETCONTRAST);
  display.ssd1306_command(0x05);    // very low brightness
  displayDimmed = true;
}

void wakeDisplay() {
  display.ssd1306_command(SSD1306_SETCONTRAST);
  display.ssd1306_command(0xCF);    // full brightness
  display.ssd1306_command(SSD1306_DISPLAYON);
  displayAwake  = true;
  displayDimmed = false;
  updateDisplay();
}

void sleepDisplay() {
  display.ssd1306_command(SSD1306_DISPLAYOFF);
  displayAwake  = false;
  displayDimmed = false;
}

// =============================================================
//  READ AD KEYBOARD
//  This calibration changes from module to module.
//  Use CALIBRATE_MODE to find your exact values.
// =============================================================
Button readADButton() {
  int v = analogRead(AD_PIN);
  if (v >= BTN_UP_MIN     && v <= BTN_UP_MAX)     return BTN_UP;
  if (v >= BTN_RIGHT_MIN  && v <= BTN_RIGHT_MAX)  return BTN_RIGHT;
  if (v >= BTN_LEFT_MIN   && v <= BTN_LEFT_MAX)   return BTN_LEFT;
  if (v >= BTN_DOWN_MIN   && v <= BTN_DOWN_MAX)   return BTN_DOWN;
  if (v >= BTN_CENTER_MIN && v <= BTN_CENTER_MAX) return BTN_CENTER;
  return NONE;
}

// =============================================================
//  FORMAT TIME  →  mm:ss
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
//
//  Layout (128 × 64):
//  ┌──────────────────────────┐  y = 0
//  │ ♪ iPod  SHF  BT✓        │  title bar (inverted)   h = 13
//  ├──────────────────────────┤  y = 13
//  │ Track 3 / 24             │  track number           y = 15
//  │ Blinding Lights...       │  scrolling title        y = 25
//  │ > Playing   02:34        │  status + time          y = 35
//  │ VOL [████████░░░] 18     │  volume bar             y = 50
//  └──────────────────────────┘  y = 63
// =============================================================
void updateDisplay() {
  if (!displayAwake) return;

  display.clearDisplay();

  // ── Title bar (inverted) ───────────────────────────────
  display.fillRect(0, 0, 128, 13, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setTextSize(1);
  display.setCursor(2, 3);
  String hdr = "\x0E iPod";
  if (shuffleMode)              hdr += " SHF";
  if (outputMode == MODE_BT)    hdr += btConnected ? " BT\x12" : " BT..";
  display.print(hdr);

  display.setTextColor(SSD1306_WHITE);

  // ── Track number ──────────────────────────────────────
  display.setCursor(0, 15);
  display.print("Track ");
  display.print(currentSong);
  display.print(" / ");
  display.print(totalSongs);

  // ── Song title (marquee if > 16 chars) ───────────────
  display.setCursor(0, 25);
  if ((int)songTitle.length() <= 16) {
    display.print(songTitle);
  } else {
    String padded  = songTitle + "   ";
    int    len     = padded.length();
    String visible = "";
    for (int i = 0; i < 16; i++)
      visible += padded[(marqueeOff + i) % len];
    display.print(visible);
  }

  // ── Playback status + elapsed time ───────────────────
  display.setCursor(0, 35);
  display.print(isPlaying ? "> " : "||");
  display.setCursor(14, 35);
  display.print(isPlaying ? "Playing " : "Paused  ");
  display.print(formatTime(elapsedSeconds));

  // ── Volume bar ────────────────────────────────────────
  display.setCursor(0, 50);
  display.print("VOL");
  display.drawRect(22, 50, 90, 8, SSD1306_WHITE);
  display.fillRect(23, 51, map(volume, 0, 30, 0, 88), 6, SSD1306_WHITE);
  display.setCursor(115, 50);
  if (volume < 10) display.print(" ");
  display.print(volume);

  display.display();
}

// =============================================================
//  SPLASH SCREEN // Input your name here:
// =============================================================
void showSplash() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(14, 10);
  display.print("ESP32");
  display.setTextSize(1);
  display.setCursor(30, 34);
  display.print("iPod v2.0");
  display.setCursor(18, 48);
  display.print("by SerialKillr :)");
  display.display();
  delay(2000);
}

// =============================================================
//  ERROR SCREEN
// =============================================================
void showError(const char *msg) {
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
