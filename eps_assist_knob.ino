// SuperATV EZ-Steer variable assist controller - DIY knob
// Board: Arduino Nano Every (ATmega4809). Uses TCA0 directly - will NOT run on an Uno.
//
// The EPS module reads assist level from the FREQUENCY of a ~50% duty square wave.
// Frequencies below are measured from a real SuperATV assist controller with a
// logic analyzer at 16/24 MHz. Duty carries no information: the OEM unit always
// outputs half-period high, half-period low (its low is a fixed 7.7 us longer,
// which is just software overhead). 100% = line held static HIGH, which is also
// what the module sees with no controller attached, and means full assist.
//
// Wiring:
//   VIN  -> switched 12 V (the EPS remote-on wire)
//   GND  -> EPS module ground terminal, and pot low side
//   5V   -> pot high side
//   A0   -> pot wiper   (200 kohm linear, Alps RV24YN 20S B204)
//                      0.1 uF (104J) wiper-to-ground if readings wander
//   Pot lead colors:   pin 1 purple = GND, pin 2 green = wiper to A0, pin 3 blue = 5V
//   D12  -> EPS assist signal wire
//
// Failure behavior: if this board loses power the line goes static, which the
// module reads as no controller = full assist.
//
// Serial Monitor: 115200 baud, line ending "Newline". Commands:
//   ?       report current step, frequency and source
//   t       print the whole table
//   k       return to knob control
//   s45     force a setting in % (0-100, rounded to 5); disables knob
//   f900    force a raw frequency in Hz; disables knob
//   v       toggle continuous reporting while the knob moves

const uint8_t PWM_PIN = 12;       // PE1 = TCA0 WO1 when TCA0 is routed to PORTE
const uint8_t POT_PIN = A0;

const uint8_t  N_STEPS = 21;      // 0,5,10 ... 100
const uint16_t STEP_PCT = 5;

// Index n = n*5 percent assist. Entry 20 (100%) is static high, flagged by 0.
const float FREQ_HZ[N_STEPS] = {
  1888.7f, 1889.0f, 1857.5f, 1737.8f, 1613.2f,   //  0  5 10 15 20
  1464.6f, 1303.4f, 1148.4f, 1004.6f,  871.0f,   // 25 30 35 40 45
   751.6f,  644.0f,  546.3f,  458.0f,  373.9f,   // 50 55 60 65 70
   297.2f,  223.9f,  156.3f,   91.1f,   28.1f,   // 75 80 85 90 95
     0.0f                                        // 100 = static high
};
// All entries measured. false = recovered from a capture with a glitchy probe
// connection (frequency is sound; its duty reading was not usable).
const bool CLEAN[N_STEPS] = {
  true,  true,  false, true,  false,
  true,  false, false, false, true,
  true,  true,  false, false, false,
  true,  false, true,  false, true,
  true
};

const uint16_t PRESC_DIV[8]  = {1, 2, 4, 8, 16, 64, 256, 1024};
const uint8_t  PRESC_BITS[8] = {
  TCA_SINGLE_CLKSEL_DIV1_gc,  TCA_SINGLE_CLKSEL_DIV2_gc,
  TCA_SINGLE_CLKSEL_DIV4_gc,  TCA_SINGLE_CLKSEL_DIV8_gc,
  TCA_SINGLE_CLKSEL_DIV16_gc, TCA_SINGLE_CLKSEL_DIV64_gc,
  TCA_SINGLE_CLKSEL_DIV256_gc, TCA_SINGLE_CLKSEL_DIV1024_gc
};

bool  knobMode   = true;
bool  verbose    = false;
int8_t step      = -1;            // 0..20, -1 before first read
float targetHz   = 0;             // 0 = static high
float actualHz   = 0;
bool  staticHigh = false;
float potEMA     = -1;

void setStaticHigh() {
  TCA0.SINGLE.CTRLB &= ~TCA_SINGLE_CMP1EN_bm;   // release the pin from the timer
  digitalWrite(PWM_PIN, HIGH);
  staticHigh = true;
  actualHz = 0;
}

void setFreq(float f) {
  if (f <= 0) { setStaticHigh(); return; }
  if (f < 1.0f)     f = 1.0f;
  if (f > 50000.0f) f = 50000.0f;

  uint8_t  pi = 0;
  uint32_t ticks = 0;
  for (pi = 0; pi < 8; pi++) {
    ticks = (uint32_t)(F_CPU / (PRESC_DIV[pi] * f) + 0.5f);
    if (ticks <= 65536UL) break;
  }
  if (ticks < 2) ticks = 2;
  uint16_t per = (uint16_t)(ticks - 1);

  TCA0.SINGLE.CTRLA = PRESC_BITS[pi] | TCA_SINGLE_ENABLE_bm;
  TCA0.SINGLE.PERBUF  = per;
  TCA0.SINGLE.CMP1BUF = (uint16_t)(ticks / 2);     // 50% duty
  TCA0.SINGLE.CTRLB  |= TCA_SINGLE_CMP1EN_bm;      // hand the pin back to the timer

  staticHigh = false;
  actualHz = (float)F_CPU / ((float)PRESC_DIV[pi] * (float)(per + 1));
}

void applyStep(int8_t n) {
  if (n < 0) n = 0;
  if (n > N_STEPS - 1) n = N_STEPS - 1;
  step = n;
  targetHz = FREQ_HZ[n];
  setFreq(targetHz);
}

uint16_t readPot() {
  analogRead(POT_PIN);                 // discard: lets the sample-and-hold settle
  return analogRead(POT_PIN);
}

void report() {
  Serial.print(F("assist "));
  if (step >= 0) { Serial.print(step * STEP_PCT); Serial.print(F("%")); }
  else Serial.print(F("--"));
  Serial.print(F("  "));
  if (staticHigh) Serial.print(F("static HIGH (full assist)"));
  else {
    Serial.print(actualHz, 2); Serial.print(F(" Hz"));
    if (step >= 0 && targetHz > 0) {
      Serial.print(F(" (target ")); Serial.print(targetHz, 1); Serial.print(F(")"));
      Serial.print(CLEAN[step] ? F(" [clean]") : F(" [deglitched]"));
    }
  }
  Serial.print(knobMode ? F("  knob") : F("  manual"));
  Serial.println();
}

void printTable() {
  Serial.println(F("  %   target Hz   source"));
  for (uint8_t i = 0; i < N_STEPS; i++) {
    Serial.print(i * STEP_PCT < 100 ? F("  ") : F(" "));
    Serial.print(i * STEP_PCT); Serial.print(F("   "));
    if (FREQ_HZ[i] == 0) Serial.print(F("static high"));
    else Serial.print(FREQ_HZ[i], 1);
    Serial.println(CLEAN[i] ? F("   clean") : F("   deglitched"));
  }
}

bool parseNum(const String &s, uint8_t skip, float &out) {
  String a = s.substring(skip); a.trim();
  if (a.length() == 0) return false;
  for (uint16_t i = 0; i < a.length(); i++) {
    char c = a.charAt(i);
    if (!isDigit(c) && c != '.') return false;
  }
  out = a.toFloat();
  return true;
}

void setup() {
  pinMode(PWM_PIN, OUTPUT);
  Serial.begin(115200);
  Serial.setTimeout(50);

  PORTMUX.TCAROUTEA = 0x04;                       // route TCA0 to PORTE; WO1 = PE1 = D12
  TCA0.SPLIT.CTRLA = 0;
  TCA0.SPLIT.CTRLESET = TCA_SPLIT_CMD_RESET_gc;
  TCA0.SINGLE.CTRLD = 0;                          // leave split mode
  TCA0.SINGLE.CTRLB = TCA_SINGLE_WGMODE_SINGLESLOPE_gc;

  uint16_t raw = readPot();
  potEMA = raw;
  applyStep((int8_t)(raw / 1023.0f * (N_STEPS - 1) + 0.5f));

  Serial.println(F("EZ-Steer assist knob. Commands: ?  t  k  s<%>  f<Hz>  v"));
  report();
}

void loop() {
  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line.length()) {
      String lo = line; lo.toLowerCase();
      char c = lo.charAt(0);
      float v;
      bool show = true;
      if (c == '?') { }
      else if (c == 't') { printTable(); show = false; }
      else if (c == 'v') { verbose = !verbose; Serial.print(F("verbose ")); Serial.println(verbose ? F("on") : F("off")); show = false; }
      else if (c == 'k') { knobMode = true; potEMA = -1; }
      else if (c == 's') {
        if (!parseNum(lo, 1, v)) { Serial.println(F("e.g. s45")); show = false; }
        else { knobMode = false; applyStep((int8_t)(constrain(v, 0, 100) / STEP_PCT + 0.5f)); }
      }
      else if (c == 'f') {
        if (!parseNum(lo, 1, v)) { Serial.println(F("e.g. f900")); show = false; }
        else { knobMode = false; step = -1; targetHz = v; setFreq(v); }
      }
      else { Serial.println(F("Use ?  t  k  s<%>  f<Hz>  v")); show = false; }
      if (show) report();
    }
  }

  if (knobMode) {
    static uint32_t last = 0;
    if (millis() - last >= 25) {
      last = millis();
      uint16_t raw = readPot();
      potEMA = (potEMA < 0) ? raw : (potEMA * 0.80f + raw * 0.20f);

      float pos = potEMA / 1023.0f * (N_STEPS - 1);     // 0.0 .. 20.0
      if (step < 0 || fabs(pos - step) > 0.65f) {       // hysteresis: no flicker at edges
        applyStep((int8_t)(pos + 0.5f));
        if (verbose) report();
      }
    }
  }
}
