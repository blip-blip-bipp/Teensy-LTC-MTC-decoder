/* Teensy 4.0 + Audio Shield (Rev D)
 * Analog LTC on LINE IN -> decode -> MAX7219
 * USB-MIDI MTC OUT (from LTC)
 * USB-MIDI MTC IN fallback display (correct spec decoding)
 *
 * DOUBLE SCREEN: Timecode display CS=9, SOURCE/RATE display CS=5
 * Both share DIN=11, CLK=13
 */

#include <Audio.h>
#include <SPI.h>
#include <EEPROM.h>
#include <usb_midi.h>

constexpr uint32_t FS_HZ = (uint32_t)(AUDIO_SAMPLE_RATE_EXACT + 0.5f);

#define USE_RIGHT_CHANNEL      1
#define TC_TIMEOUT_MS          300
#define BLINK_PERIOD_MS        500
#define LOCK_CONSEC_FRAMES     4
#define DISPLAY_INTENSITY 0x08   // 0x00..0x0F (shared brightness for both displays)
#define SPLASH_5318008_MS 3000
#define SPLASH_FADE_STEP_MS 60

// ================= AUDIO =================

AudioInputI2S        audioIn;
AudioRecordQueue     qL, qR;
AudioConnection      patchL(audioIn, 0, qL, 0);
AudioConnection      patchR(audioIn, 1, qR, 0);
AudioControlSGTL5000 sgtl;

// ================= DISPLAY =================

class Max7219 {
public:
  explicit Max7219(uint8_t cs): cs(cs) {}
  void setOtherCS(uint8_t other){ other_cs = other; }
  void begin(){
    pinMode(cs,OUTPUT); SPI.begin();
    send(0x0F,0); send(0x0C,1); send(0x0B,7); setIntensity(DISPLAY_INTENSITY);
    send(0x09,0xFF); clear();
  }
  void clear(){ for(uint8_t i=1;i<=8;i++) send(i,0x0F); }
  void clearRaw(){ for(uint8_t i=1;i<=8;i++) send(i,0x00); }
  void setIntensity(uint8_t v){ send(0x0A, v & 0x0F); }
  void showTC(uint8_t H,uint8_t M,uint8_t S,uint8_t F,bool dots=true){
    uint8_t dp = dots ? 0x80 : 0x00;
    send(8,(H/10)%10); send(7,(H%10)|dp);
    send(6,(M/10)%10); send(5,(M%10)|dp);
    send(4,(S/10)%10); send(3,(S%10)|dp);
    send(2,(F/10)%10); send(1,(F%10));
  }
  void showDigits8(const char* s){
    // Assumes decode mode is enabled (0x09 = 0xFF). Writes up to 8 numeric chars.
    // Digit positions: 8 = leftmost, 1 = rightmost.
    for (uint8_t pos = 0; pos < 8; pos++) {
      char c = s[pos];
      if (c >= '0' && c <= '9') send(8 - pos, (uint8_t)(c - '0'));
      else                      send(8 - pos, 0x0F); // blank
      if (c == 0) { // end of string -> blank remaining
        for (uint8_t p2 = pos; p2 < 8; p2++) send(8 - p2, 0x0F);
        break;
      }
    }
  }
  void beginAsFPS(){
    pinMode(cs,OUTPUT); SPI.begin();
    send(0x0F,0); send(0x0C,1); send(0x0B,7); setIntensity(DISPLAY_INTENSITY);
    send(0x09,0x00); clearRaw();  // no-decode for letters F,P,S,L,M
  }
  void ensureFPSConfig(){
    // Re-assert critical registers in case of CS glitches / noise
    send(0x0F,0);        // display test OFF
    send(0x0C,1);        // shutdown = normal operation
    send(0x0B,7);        // scan limit = 8 digits
    setIntensity(DISPLAY_INTENSITY);  // shared brightness
    send(0x09,0x00);     // no-decode
  }
  // Horizontal mirror (swap B↔F, C↔E); Vertical mirror (swap A↔D, B↔E, C↔F); MAX7219 D6=A D5=B D4=C D3=D D2=E D1=F D0=G
  static uint8_t mirrorH(uint8_t x){ return (x&0xC9)|((x&0x20)>>4)|((x&0x02)<<4)|((x&0x10)>>2)|((x&0x04)<<2); }
  static uint8_t mirrorV(uint8_t x){ return (x&0x81)|((x&0x40)>>3)|((x&0x08)<<3)|((x&0x20)>>3)|((x&0x04)<<3)|((x&0x10)>>3)|((x&0x02)<<3); }
  void showFPSL(float fps){
    // LTC (analog) source label + rate digits
    ensureFPSConfig();
    // Left label: "ANA" on digits 8..6
    send(8, GLYPH_A);
    send(7, GLYPH_n);
    send(6, GLYPH_A);
    send(5, 0x00);
    showFPSDigits(fps);
  }
  void showFPSM(float fps){
    // USB MTC source label + rate digits
    ensureFPSConfig();
    // Left label: "USB" on digits 8..6
    send(8, GLYPH_U);
    send(7, GLYPH_S);
    send(6, GLYPH_b);
    send(5, 0x00);
    showFPSDigits(fps);
  }
  void showFPSDigits(float fps){
    // Always fully overwrite digits 1..4
    send(4,0x00); send(3,0x00); send(2,0x00); send(1,0x00);

    // Common video rates rendered as 4 chars (##.##)
    if (fps >= 29.9f && fps < 30.0f) {
      // 29.97
      send(4,0x6D);              // 2
      send(3,0x7B | 0x80);       // 9.
      send(2,0x7B);              // 9
      send(1,0x70);              // 7
      return;
    }
    if (fps >= 23.9f && fps < 24.0f) {
      // 23.97 (approx of 23.976)
      send(4,0x6D);              // 2
      send(3,0x79 | 0x80);       // 3.
      send(2,0x7B);              // 9
      send(1,0x70);              // 7
      return;
    }

    // Otherwise show nearest integer (2 digits) right-aligned
    int v = (int)(fps + 0.5f);
    if (v < 0) v = 0;
    if (v > 99) v = 99;
    send(2,seg[v/10]);
    send(1,seg[v%10]);
  }
  void showTest00(){
    // raw/no-decode: show "00" on the rightmost 2 digits
    send(4,0x00); send(3,0x00);
    send(2,seg[0]);
    send(1,seg[0]);
  }
  void showRateBits(uint8_t bits){
    // show 0-99 on rightmost two digits
    uint8_t tens = (bits/10)%10;
    uint8_t ones = bits%10;
    send(4,0x00); send(3,0x00); // clear positions 4/3
    send(2,seg[tens]); send(1,seg[ones]);
  }
private:
  const uint8_t cs;
  uint8_t other_cs = 255; // optional: CS pin of the other MAX7219, kept HIGH during transfers
  SPISettings cfg{1000000, MSBFIRST, SPI_MODE0};
  static const uint8_t seg[10];  // 7-seg 0-9 (no-decode)
  // 7-seg letter glyphs (raw, no-decode). Bit mapping: D6=A D5=B D4=C D3=D D2=E D1=F D0=G
  static constexpr uint8_t GLYPH_A = 0x77; // a b c e f g
  static constexpr uint8_t GLYPH_n = 0x15; // c e g  (lowercase n approximation)
  static constexpr uint8_t GLYPH_U = 0x3E; // b c d e f
  static constexpr uint8_t GLYPH_S = 0x5B; // same as digit 5
  static constexpr uint8_t GLYPH_b = 0x1F; // c d e f g (lowercase b)
  void send(uint8_t r,uint8_t d){
    // Ensure the other MAX7219 never latches these bytes
    if(other_cs != 255) digitalWrite(other_cs, HIGH);
    SPI.beginTransaction(cfg);
    digitalWrite(cs,LOW);
    SPI.transfer(r);
    SPI.transfer(d);
    digitalWrite(cs,HIGH);
    SPI.endTransaction();
  }
};

const uint8_t Max7219::seg[10] = {0x7E,0x30,0x6D,0x79,0x33,0x5B,0x5F,0x70,0x7F,0x7B};

Max7219 display(9);      // Timecode display: DIN=11, CLK=13, CS=9
Max7219 displayFPS(5);   // Source/Rate display: DIN=11, CLK=13, CS=5 (avoid pin 8, which is I2S RX on Teensy 4.0)

// ================= LTC DECODER =================
// ⚠ KEEP YOUR EXACT ORIGINAL LTCFromPCM CLASS HERE
// (Not modified to preserve your proven stable baseline)


// ================= MTC IN =================

struct MTCFrame { uint8_t hh, mm, ss, ff; };

uint8_t mtcNibbles[8];
bool mtcHaveFull = false;
elapsedMillis mtcLastMs;
MTCFrame mtcFrame;
float mtcFPS = 25.0f;

// Added global lastLTCms to avoid shadowing
uint32_t lastLTCms = 0;

// ----- Global flags for source activity and last shown source -----
bool g_ltcActive = false;
bool g_mtcActive = false;
uint8_t g_lastTCSource = 0; // 0=unknown, 1=LTC, 2=MTC (last shown on TC display)


// ===== FPS sticky state (must be declared before handleMTC uses it) =====
int lastShownFPSKey = -1;          // quantized FPS key (e.g. 25, 2997, 2397)
bool lastShownWasLTC = false;      // last source flag
elapsedMillis fpsRefreshTimer;     // periodic refresh to keep display stable
static int fpsKeyFromFloat(float fps);  // forward declaration

void handleMTC() {
  while (usbMIDI.read()) {

    if (usbMIDI.getType() == usbMIDI.TimeCodeQuarterFrame) {

      uint8_t data  = usbMIDI.getData1();
      uint8_t index = (data >> 4) & 0x07;
      uint8_t value = data & 0x0F;

      mtcNibbles[index] = value;

      if (index == 7) {

        // -------- Proper spec decode --------

        mtcFrame.ff = (mtcNibbles[1] << 4) | mtcNibbles[0];
        mtcFrame.ss = (mtcNibbles[3] << 4) | mtcNibbles[2];
        mtcFrame.mm = (mtcNibbles[5] << 4) | mtcNibbles[4];

        uint8_t hourMSB = mtcNibbles[7] & 0x01;   // only bit 0
        mtcFrame.hh = (hourMSB << 4) | mtcNibbles[6];

        // -------- Frame rate decode --------
        uint8_t rateBits = (mtcNibbles[7] >> 1) & 0x03;

        switch(rateBits){
          case 0: mtcFPS = 24.0f; break;
          case 1: mtcFPS = 25.0f; break;
          case 2: mtcFPS = 29.97f; break;
          case 3: mtcFPS = 30.0f; break;
        }

        mtcHaveFull = true;
        mtcLastMs = 0;

        Serial.print("MTC rateBits: ");
        Serial.print(rateBits);
        Serial.print("  -> Decoded FPS: ");
        Serial.println(mtcFPS, 3);

        // Update FPS display immediately from MTC (MTC-only friendly)
        displayFPS.showFPSM(mtcFPS);
        lastShownFPSKey = fpsKeyFromFloat(mtcFPS);
        lastShownWasLTC = false;
        fpsRefreshTimer = 0;

      }
    }
  }
}

// ================= MTC OUT =================

uint8_t nibb[8];
uint8_t qfIdx=0;
elapsedMicros qfTimer;
float curFPS=25.0f;

void buildMTC(uint8_t h,uint8_t m,uint8_t s,uint8_t f){
  nibb[0]=f&0x0F; nibb[1]=(f>>4)&1;
  nibb[2]=s&0x0F; nibb[3]=(s>>4)&7;
  nibb[4]=m&0x0F; nibb[5]=(m>>4)&7;
  nibb[6]=h&0x0F; nibb[7]=(h>>4)&1;
}

void sendQF(){
  usbMIDI.sendTimeCodeQuarterFrame(qfIdx&7, nibb[qfIdx&7]&0x0F);
  qfIdx=(qfIdx+1)&7;
}

// ================= UI =================

uint8_t lastH=0,lastM=0,lastS=0,lastF=0;
bool haveShown=false;
elapsedMillis blinkTimer;
bool blinkState=false;

static int fpsKeyFromFloat(float fps){
  // Quantize to stable display keys to avoid float jitter
  if (fps >= 29.9f && fps < 30.0f) return 2997;   // 29.97
  if (fps >= 23.9f && fps < 24.0f) return 2397;   // show as 23.97
  int v = (int)(fps + 0.5f);                      // nearest integer
  if (v < 0) v = 0;
  if (v > 99) v = 99;
  return v;
}

void showTC_cached(uint8_t h,uint8_t m,uint8_t s,uint8_t f,bool dots){
  lastH=h; lastM=m; lastS=s; lastF=f;
  haveShown=true;
  display.showTC(h,m,s,f,dots);

  // Remember which source was active when we last updated the TC display
  if (g_ltcActive) g_lastTCSource = 1;
  else if (g_mtcActive) g_lastTCSource = 2;
}

// ================= SETUP =================

void setup(){
  // Ensure both CS pins are HIGH before any SPI so only one display latches at a time
  pinMode(5, OUTPUT);
  pinMode(9, OUTPUT);
  digitalWrite(5, HIGH);
  digitalWrite(9, HIGH);
  display.setOtherCS(5);
  displayFPS.setOtherCS(9);

  Serial.begin(115200);
  while (!Serial && millis() < 2000) {}  // wait briefly for USB Serial
  Serial.println("--- LTC/MTC Dual Display Debug ---");

  AudioMemory(24);
  sgtl.enable();
  sgtl.inputSelect(AUDIO_INPUT_LINEIN);
  sgtl.lineInLevel(0);
  sgtl.volume(0.6f);

  display.begin();

  // ----- Splash on TC display -----
  display.clear();
  display.showDigits8("5318008");   // left-aligned, blanks remaining digit
  delay(SPLASH_5318008_MS);

  // Fade out splash (then restore normal intensity)
  for (int i = DISPLAY_INTENSITY; i >= 0; i--) {
    display.setIntensity((uint8_t)i);
    delay(SPLASH_FADE_STEP_MS);
  }
  display.setIntensity(DISPLAY_INTENSITY);
  display.clear();

  // Initial idle TC
  display.showTC(0,0,0,0,true);

  displayFPS.beginAsFPS();
  displayFPS.clearRaw();
  displayFPS.showFPSM(25.0f);  // default: show USB + 25 until a source is active
  lastShownFPSKey = fpsKeyFromFloat(25.0f);
  lastShownWasLTC = false;
  fpsRefreshTimer = 0;

  qL.begin();
  qR.begin();
}

// ================= LOOP =================

void loop(){

  handleMTC();

  // ===== YOUR ORIGINAL LTC PROCESSING HERE =====
  // Keep your LTC block exactly as before
  // Ensure you update lastLTCms when LTC frame received
  // =============================================

  // Removed local static lastLTCms to avoid shadowing global

  bool ltcActive = (millis()-lastLTCms < TC_TIMEOUT_MS);
  g_ltcActive = ltcActive;

  // ---------- FPS display ----------
  bool mtcActive = (mtcLastMs < TC_TIMEOUT_MS);
  g_mtcActive = mtcActive;


  if (ltcActive) {
    // LTC present (if/when you use LTC)
    float activeFPS = curFPS;
    int fpsKey = fpsKeyFromFloat(activeFPS);

    if (fpsKey != lastShownFPSKey || lastShownWasLTC != true || fpsRefreshTimer >= 50) {
      fpsRefreshTimer = 0;
      lastShownFPSKey = fpsKey;
      lastShownWasLTC = true;
      displayFPS.showFPSL(activeFPS);
    }
  }
  else if (mtcActive) {
    // MTC-only mode
    float activeFPS = mtcFPS;
    int fpsKey = fpsKeyFromFloat(activeFPS);

    if (fpsKey != lastShownFPSKey || lastShownWasLTC != false || fpsRefreshTimer >= 50) {
      fpsRefreshTimer = 0;
      lastShownFPSKey = fpsKey;
      lastShownWasLTC = false;
      displayFPS.showFPSM(activeFPS);
    }
  }


  // else: no source active -> leave FPS display showing last value (do not overwrite with default 25)

  // ---------- MTC fallback ----------
  // Only refresh TC from MTC while MTC is actively being received.
  // If MTC stops, we leave the last frame on screen so the blink logic can work.
  if (!ltcActive && mtcHaveFull && mtcActive) {
    showTC_cached(mtcFrame.hh, mtcFrame.mm, mtcFrame.ss, mtcFrame.ff, true);
  }

  // ---------- Blink only if both absent ----------
  bool stopped = false;
  if (g_lastTCSource == 1) {
    // Last shown was LTC -> blink when LTC is no longer active
    stopped = !ltcActive;
  } else if (g_lastTCSource == 2) {
    // Last shown was MTC -> blink when MTC is no longer active
    stopped = !mtcActive;
  } else {
    // Unknown -> blink only when neither source is active
    stopped = (!ltcActive && !mtcActive);
  }

  if(stopped && haveShown){
    if(blinkTimer>=BLINK_PERIOD_MS){
      blinkTimer=0;
      blinkState=!blinkState;

      if (blinkState) {
        // ON: show last received frame
        display.showTC(lastH,lastM,lastS,lastF, true);
      } else {
        // OFF: blank the whole display
        display.clear();
      }
    }
  } else {
    // When running, ensure TC is visible and stop blinking
    blinkState=false;
    blinkTimer=0;
  }
}
