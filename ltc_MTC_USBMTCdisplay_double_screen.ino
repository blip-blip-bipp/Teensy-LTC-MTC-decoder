#include <Arduino.h>
#include <Audio.h>
#include "analyze_ltc.h"
#include <SPI.h>

SPISettings maxSpi(1000000, MSBFIRST, SPI_MODE0);

// MAX7219 pins
#define DIN_PIN 11
#define CLK_PIN 13
#define CS_TC   9   // Timecode display
#define CS_FPS  5   // FPS/status display

// 7-seg raw encoding (decode mode OFF)
static const uint8_t seg[10] = {
  0x7E, // 0
  0x30, // 1
  0x6D, // 2
  0x79, // 3
  0x33, // 4
  0x5B, // 5
  0x5F, // 6
  0x70, // 7
  0x7F, // 8
  0x7B  // 9
};

// ---------- Forward declarations ----------
void maxSend(uint8_t cs, uint8_t reg, uint8_t data);
void maxInit(uint8_t cs);
void displayTC(uint8_t h, uint8_t m, uint8_t s, uint8_t f, bool dots = true);
void displayFPS(float fps, bool isUSB = false);
void blankTC();
void displayBoobies();
void funkyTest();
void handleMTC();
void showTC_cached(uint8_t h, uint8_t m, uint8_t s, uint8_t f, bool dots);
uint8_t mtcRateBitsFromFPS(float fps);
void sendMTCFullFrame(uint8_t h, uint8_t m, uint8_t s, uint8_t f, float fps);
void sendMTCQuarterFrameSeq(uint8_t h, uint8_t m, uint8_t s, uint8_t f, float fps);
void buildMTC(uint8_t h, uint8_t m, uint8_t s, uint8_t f);

// ---------- Splash helpers ----------
void displayBoobies() {
  // Temporarily enable BCD decode on the TC display for clean numeric splash
  maxSend(CS_TC, 0x09, 0xFF);

  // Clear all digits first
  for (int i = 1; i <= 8; i++) {
    maxSend(CS_TC, i, 0x0F);
  }

  // 5318008 (upside down reads BOOBIES) with no leading zero
  maxSend(CS_TC, 7, 5);
  maxSend(CS_TC, 6, 3);
  maxSend(CS_TC, 5, 1);
  maxSend(CS_TC, 4, 8);
  maxSend(CS_TC, 3, 0);
  maxSend(CS_TC, 2, 0);
  maxSend(CS_TC, 1, 8);
}

void funkyTest() {
  static uint8_t step = 0;
  for (int i = 1; i <= 8; i++) {
    maxSend(CS_FPS, i, (step + i) & 0x0F);
  }
  step++;
}

// ---------- MAX7219 helpers ----------
void maxSend(uint8_t cs, uint8_t reg, uint8_t data) {
  SPI.beginTransaction(maxSpi);
  digitalWrite(CS_TC, HIGH);
  digitalWrite(CS_FPS, HIGH);
  digitalWrite(cs, LOW);
  SPI.transfer(reg);
  SPI.transfer(data);
  digitalWrite(cs, HIGH);
  SPI.endTransaction();
}

void maxInit(uint8_t cs) {
  pinMode(cs, OUTPUT);
  digitalWrite(cs, HIGH);
  maxSend(cs, 0x0F, 0x00);  // display test off
  maxSend(cs, 0x0C, 0x01);  // shutdown off
  maxSend(cs, 0x0B, 0x07);  // scan limit 8 digits
  maxSend(cs, 0x0A, 0x08);  // intensity
  maxSend(cs, 0x09, 0x00);  // raw mode for both displays
  for (int i = 1; i <= 8; i++) maxSend(cs, i, 0x00);
}

void blankTC() {
  for (int i = 1; i <= 8; i++) maxSend(CS_TC, i, 0x00);
}

void displayTC(uint8_t h, uint8_t m, uint8_t s, uint8_t f, bool dots) {
  uint8_t d7 = seg[h % 10];
  uint8_t d5 = seg[m % 10];
  uint8_t d3 = seg[s % 10];

  if (dots) {
    d7 |= 0x80;
    d5 |= 0x80;
    d3 |= 0x80;
  }

  maxSend(CS_TC, 8, seg[h / 10]);
  maxSend(CS_TC, 7, d7);
  maxSend(CS_TC, 6, seg[m / 10]);
  maxSend(CS_TC, 5, d5);
  maxSend(CS_TC, 4, seg[s / 10]);
  maxSend(CS_TC, 3, d3);
  maxSend(CS_TC, 2, seg[f / 10]);
  maxSend(CS_TC, 1, seg[f % 10]);
}

void displayFPS(float fps, bool isUSB) {
  const uint8_t GLYPH_A = 0x77;
  const uint8_t GLYPH_n = 0x15;
  const uint8_t GLYPH_U = 0x3E;
  const uint8_t GLYPH_S = 0x5B;
  const uint8_t GLYPH_b = 0x1F;

  uint8_t d[8] = {0};

  if (isUSB) {
    d[7] = GLYPH_U;
    d[6] = GLYPH_S;
    d[5] = GLYPH_b;
  } else {
    d[7] = GLYPH_A;
    d[6] = GLYPH_n;
    d[5] = GLYPH_A;
  }

  if (fps > 29.5f && fps < 30.0f) {
    // 29.97
    d[3] = seg[2];
    d[2] = seg[9] | 0x80;
    d[1] = seg[9];
    d[0] = seg[7];
  } else {
    int v = (int)(fps + 0.5f);
    if (v < 0) v = 0;
    if (v > 99) v = 99;
    d[1] = seg[v / 10];
    d[0] = seg[v % 10];
  }

  for (int i = 0; i < 8; i++) {
    maxSend(CS_FPS, i + 1, d[i]);
  }
}

// ---------- Audio / LTC ----------
AudioInputI2S        audioIn;
AudioOutputUSB       usbAudioOut;
AudioAnalyzeLTC      ltc1;
AudioConnection      patchCord1(audioIn, 0, ltc1, 0);
AudioConnection      patchCord2(audioIn, 0, usbAudioOut, 0);
AudioConnection      patchCord3(audioIn, 1, usbAudioOut, 1);
AudioControlSGTL5000 sgtl;
ltcframe_t ltcframe;

// ---------- Runtime state ----------
uint32_t lastLTCms = 0;
float    curFPS = 25.0f;

// USB MTC state
uint8_t  mtcNibbles[8] = {0};
bool     mtcHaveFrame = false;
uint32_t lastMTCms = 0;
uint8_t  mtcH = 0, mtcM = 0, mtcS = 0, mtcF = 0;
float    mtcFPS = 25.0f;

// Pause / blink state
bool     blinkState = true;
uint32_t lastBlinkMs = 0;

// Last valid displayed frame
uint8_t  lastH = 0, lastM = 0, lastS = 0, lastF = 0;
float    lastFPS = 0;
bool     lastWasUSB = false;

// LTC validation on reacquire
bool     ltcValid = false;
uint8_t  lastSeenF = 0;

// Active source latch
bool     useLTC = true;

// USB MTC OUT state (generated only from validated LTC)
bool     mtcOutLocked = false;
uint8_t  lastSentH = 255, lastSentM = 255, lastSentS = 255, lastSentF = 255;
float    lastSentFPS = -1.0f;

// ---------- Setup ----------
void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(CS_TC, OUTPUT);
  pinMode(CS_FPS, OUTPUT);
  digitalWrite(CS_TC, HIGH);
  digitalWrite(CS_FPS, HIGH);

  SPI.begin();
  maxInit(CS_TC);
  maxInit(CS_FPS);

  // 3s splash
  uint32_t splashStart = millis();
  while (millis() - splashStart < 3000) {
    displayBoobies();
    funkyTest();
    delay(100);
  }

  // Return TC display to raw mode after splash
  maxSend(CS_TC, 0x09, 0x00);

  // startup idle display
  displayTC(0, 0, 0, 0, true);
  displayFPS(0, false);

  AudioMemory(60);
  sgtl.enable();
  sgtl.inputSelect(AUDIO_INPUT_LINEIN);
  sgtl.lineInLevel(0);
  sgtl.volume(0.5);
  sgtl.unmuteLineout();
}

// ---------- USB MTC parsing ----------
void handleMTC() {
  while (usbMIDI.read()) {
    if (usbMIDI.getType() == usbMIDI.TimeCodeQuarterFrame) {
      uint8_t data  = usbMIDI.getData1();
      uint8_t index = (data >> 4) & 0x07;
      uint8_t value = data & 0x0F;

      mtcNibbles[index] = value;

      if (index == 7) {
        mtcF = (mtcNibbles[1] << 4) | mtcNibbles[0];
        mtcS = (mtcNibbles[3] << 4) | mtcNibbles[2];
        mtcM = (mtcNibbles[5] << 4) | mtcNibbles[4];
        mtcH = ((mtcNibbles[7] & 1) << 4) | mtcNibbles[6];

        uint8_t rateBits = (mtcNibbles[7] >> 1) & 0x03;
        switch (rateBits) {
          case 0: mtcFPS = 24.0f;   break;
          case 1: mtcFPS = 25.0f;   break;
          case 2: mtcFPS = 29.97f;  break;
          case 3: mtcFPS = 30.0f;   break;
        }

        mtcHaveFrame = true;
        lastMTCms = millis();
      }
    }
  }
}

// ---------- Main loop ----------
void loop() {
  handleMTC();

  // source activity windows
  bool ltcActive = (millis() - lastLTCms < 500);
  bool mtcActive = (millis() - lastMTCms < 200);

  // source latch: LTC has priority, USB takes over only when LTC gone
  if (ltcActive) useLTC = true;
  else if (mtcActive) useLTC = false;

  if (!ltcActive) {
    ltcValid = false;
    mtcOutLocked = false;
  }

  // ----- LTC path -----
  if (ltc1.available()) {
    ltcframe = ltc1.read();

    uint8_t hh = ltc1.hour(&ltcframe);
    uint8_t mm = ltc1.minute(&ltcframe);
    uint8_t ss = ltc1.second(&ltcframe);
    uint8_t ff = ltc1.frame(&ltcframe);

    // FPS detection: latch once per second using max frame index + DF bit
    static uint8_t maxFF = 0;
    static uint8_t lastSec = 255;

    if (ff > maxFF) maxFF = ff;

    if (ss != lastSec) {
      bool dropFrame = ((uint64_t)ltcframe.data >> 10) & 1;
      if (maxFF >= 29) {
        curFPS = dropFrame ? 29.97f : 30.0f;
      } else if (maxFF >= 24) {
        curFPS = 25.0f;
      } else {
        curFPS = 24.0f;
      }
      maxFF = ff;
      lastSec = ss;
    }

    // Validate frames on reacquire: ignore zero frame and require forward motion
    if (!ltcValid) {
      if (!(hh == 0 && mm == 0 && ss == 0 && ff == 0)) {
        ltcValid = true;
        lastSeenF = ff;

        if (useLTC) {
          showTC_cached(hh, mm, ss, ff, true);
          buildMTC(hh, mm, ss, ff);
          displayFPS(curFPS, false);

          lastH = hh;
          lastM = mm;
          lastS = ss;
          lastF = ff;
          lastFPS = curFPS;
          lastWasUSB = false;
        }
      }
    } else {
      bool sameFrame = (ff == lastSeenF);
      bool nextFrame = (ff == (uint8_t)(lastSeenF + 1));
      bool wrapped   = (ff == 0 && lastSeenF >= 20);
      bool forward   = sameFrame || nextFrame || wrapped;

      if (forward && useLTC) {
        showTC_cached(hh, mm, ss, ff, true);
        buildMTC(hh, mm, ss, ff);
        displayFPS(curFPS, false);

        lastH = hh;
        lastM = mm;
        lastS = ss;
        lastF = ff;
        lastFPS = curFPS;
        lastWasUSB = false;
      }
      if (forward) {
        lastSeenF = ff;
      }
    }

    lastLTCms = millis();
    ltcActive = true;
    useLTC = true;
  }

  // ----- USB MTC fallback -----
  static uint8_t pmH = 255, pmM = 255, pmS = 255, pmF = 255;
  if (!useLTC && mtcActive && mtcHaveFrame) {
    if (mtcH != pmH || mtcM != pmM || mtcS != pmS || mtcF != pmF) {
      showTC_cached(mtcH, mtcM, mtcS, mtcF, true);
      displayFPS(mtcFPS, true);

      lastH = mtcH;
      lastM = mtcM;
      lastS = mtcS;
      lastF = mtcF;
      lastFPS = mtcFPS;
      lastWasUSB = true;

      pmH = mtcH;
      pmM = mtcM;
      pmS = mtcS;
      pmF = mtcF;
    }
  }

  // ----- idle blink: only when no valid source -----
  if (!ltcActive && !mtcActive) {
    if (millis() - lastBlinkMs > 500) {
      lastBlinkMs = millis();
      blinkState = !blinkState;
    }

    if (blinkState) {
      displayTC(lastH, lastM, lastS, lastF, true);
    } else {
      blankTC();
    }

    static uint32_t fpsRefresh = 0;
    if (millis() - fpsRefresh > 200) {
      fpsRefresh = millis();
      displayFPS(lastFPS, lastWasUSB);
    }
  }
}

// ---------- Display/cache helpers ----------
void showTC_cached(uint8_t h, uint8_t m, uint8_t s, uint8_t f, bool dots) {
  static uint8_t ph = 255, pm = 255, ps = 255, pf = 255;
  if (h == ph && m == pm && s == ps && f == pf) return;
  ph = h; pm = m; ps = s; pf = f;
  displayTC(h, m, s, f, dots);
}

uint8_t mtcRateBitsFromFPS(float fps) {
  if (fps > 29.5f && fps < 30.0f) return 2;
  if (fps > 29.5f) return 3;
  if (fps > 24.5f) return 1;
  return 0;
}

void sendMTCFullFrame(uint8_t h, uint8_t m, uint8_t s, uint8_t f, float fps) {
  uint8_t rateBits = mtcRateBitsFromFPS(fps);
  uint8_t msg[10] = {
    0xF0,
    0x7F,
    0x7F,
    0x01,
    0x01,
    (uint8_t)(((rateBits & 0x03) << 5) | (h & 0x1F)),
    (uint8_t)(m & 0x3F),
    (uint8_t)(s & 0x3F),
    (uint8_t)(f & 0x1F),
    0xF7
  };
  usbMIDI.sendSysEx(sizeof(msg), msg, true);
  usbMIDI.send_now();
}

void sendMTCQuarterFrameSeq(uint8_t h, uint8_t m, uint8_t s, uint8_t f, float fps) {
  uint8_t rateBits = mtcRateBitsFromFPS(fps);

  usbMIDI.sendTimeCodeQuarterFrame(0, f & 0x0F);
  usbMIDI.sendTimeCodeQuarterFrame(1, (f >> 4) & 0x01);
  usbMIDI.sendTimeCodeQuarterFrame(2, s & 0x0F);
  usbMIDI.sendTimeCodeQuarterFrame(3, (s >> 4) & 0x03);
  usbMIDI.sendTimeCodeQuarterFrame(4, m & 0x0F);
  usbMIDI.sendTimeCodeQuarterFrame(5, (m >> 4) & 0x03);
  usbMIDI.sendTimeCodeQuarterFrame(6, h & 0x0F);
  usbMIDI.sendTimeCodeQuarterFrame(7, (uint8_t)(((h >> 4) & 0x01) | ((rateBits & 0x03) << 1)));
  usbMIDI.send_now();
}

void buildMTC(uint8_t h, uint8_t m, uint8_t s, uint8_t f) {
  bool changed = (h != lastSentH) || (m != lastSentM) || (s != lastSentS) || (f != lastSentF);
  if (!changed) return;

  bool discontinuity = false;
  if (mtcOutLocked) {
    bool sameSecond = (h == lastSentH) && (m == lastSentM) && (s == lastSentS);
    bool frameWrap  = (h == lastSentH) && (m == lastSentM) && ((uint8_t)(lastSentS + 1) == s) && (f == 0);
    bool sequential = (sameSecond && (f == (uint8_t)(lastSentF + 1))) || frameWrap;
    discontinuity = !sequential;
  }

  if (!mtcOutLocked || discontinuity || curFPS != lastSentFPS) {
    sendMTCFullFrame(h, m, s, f, curFPS);
    mtcOutLocked = true;
  }

  sendMTCQuarterFrameSeq(h, m, s, f, curFPS);

  lastSentH = h;
  lastSentM = m;
  lastSentS = s;
  lastSentF = f;
  lastSentFPS = curFPS;
}
