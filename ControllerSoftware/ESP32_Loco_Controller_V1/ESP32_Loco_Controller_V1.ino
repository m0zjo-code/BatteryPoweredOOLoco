/*
  ESP32 WiFi Hotspot + Bidirectional Motor Speed Controller
  -----------------------------------------------------------
  Drives a DC motor (e.g. an OO gauge loco) via a TC1508A-style dual
  H-bridge driver using TWO PWM pins instead of PWM + direction pins:

    - MOTOR_PLUS_PIN  -> driver IN1 (motor+ side) - PWM when running forward
    - MOTOR_MINUS_PIN -> driver IN2 (motor- side) - PWM when running reverse

  Only one of the two pins is ever driven with a non-zero PWM signal at a
  time; the other is held at 0 (LOW), matching the TC1508A truth table:
    both LOW      = standby / coast
    IN1 PWM, IN2 LOW = forward, speed = IN1 duty
    IN2 PWM, IN1 LOW = reverse, speed = IN2 duty
    both HIGH     = brake (not used here)

  A separate DEBUG_LED_PIN outputs a PWM signal proportional to the
  MAGNITUDE of whichever motor pin is active (i.e. abs(speed)), regardless
  of direction, so a single LED gives you an at-a-glance "how fast" read-out
  during testing, similar to a VU meter.

  Hosts its own WiFi access point and a mobile-friendly single page with:
    - POWER button (starts DISABLED until the page has confirmed the
      real device state, and gates ALL output to zero when off)
    - SPEED slider from 0-100%, like a real locomotive throttle
    - REVERSER toggle switch (UP = forward, DOWN = reverse), like the
      physical reverser lever on a real locomotive. To mirror the real
      thing - and avoid a harsh mechanical snap-reversal - the reverser
      can only be moved while speed is at 0%.

  NOTE ON WIRING / PIN CHOICE:
  GPIO 8 (used here for the debug LED) is unavailable as GPIO on classic
  ESP32-WROOM/WROVER modules (reserved for SPI flash). It's fine on
  ESP32-S2/S3/C3 boards. Change DEBUG_LED_PIN if needed for your board.
*/

#include <WiFi.h>
#include <WebServer.h>

// ---------- CONFIGURATION ----------
const char* AP_SSID     = "ESP32-Controller";
const char* AP_PASSWORD = "controller123";   // must be 8+ chars, or "" for open network

const int MOTOR_PLUS_PIN  = 7;   // driver IN1 - PWM for forward
const int MOTOR_MINUS_PIN = 6;   // driver IN2 - PWM for reverse
const int DEBUG_LED_PIN   = 8;   // shows abs(speed) as brightness

const int MOTOR_FREQ = 20000;  // Hz - above audible range to avoid motor whine
const int MOTOR_RES  = 10;     // bits - 0-1023 steps for finer low-speed control

// Motors don't move at all below a certain duty cycle - there's not enough
// voltage to overcome static friction/cogging, so everything below this
// point is "wasted" slider travel (this is why you were seeing no movement
// until ~70%). Set this to the lowest duty (as a %) that reliably gets the
// loco moving from a standstill - test on your own loco/track and tune it.
// The slider's 1-100% range is then remapped onto MOTOR_MIN_DUTY_PERCENT-100%
// of actual PWM duty, so the whole slider travel does something useful.
const int MOTOR_MIN_DUTY_PERCENT = 65;  // <-- tune this to your loco

const int LED_FREQ = 5000;     // Hz - fine for an indicator LED
const int LED_RES  = 8;        // bits - 0-255 steps

// From earlier testing: this LED is wired "inverted" (sinking), so 0 duty
// must mean full HIGH on the pin, not full LOW. Flip if you rewire it.
const bool INVERT_DEBUG_LED = true;

// ---------- STATE ----------
bool powerOn = false;     // starts OFF - loco disabled by default
int  targetSpeedPercent  = 0;    // 0-100, remembered even when power is off
bool forward = true;

// ---------- Momentum ----------
const unsigned long RAMP_UP_TIME_MS=3000;
const unsigned long RAMP_DOWN_TIME_MS=3000;
float currentSpeedPercent=0.0f;
unsigned long lastRampUpdate=0;      // reverser position - only changeable while targetSpeedPercent  == 0

// Pre-compute ramp rates
float up=RAMP_UP_TIME_MS?100.0f/(RAMP_UP_TIME_MS/1000.0f):1000.0f;
float down=RAMP_DOWN_TIME_MS?100.0f/(RAMP_DOWN_TIME_MS/1000.0f):1000.0f;

WebServer server(80);

// ---------- APPLY OUTPUT ----------
void updateMotorRamp() {

  // Get current time
  unsigned long now=millis();

  // Check to see if we have updated this millisecond
  if(lastRampUpdate==0){
    lastRampUpdate=now;
    return;
  }

  // Compute delta time
  float dt=(now-lastRampUpdate)/1000.0f;

  // Update last update time
  lastRampUpdate=now;

  // Compute current target
  float target=powerOn?targetSpeedPercent:0.0f;

  // Assign new speed
  if(currentSpeedPercent<target){
    currentSpeedPercent+=up*dt; 
    if(currentSpeedPercent>target)
    {
      currentSpeedPercent=target;
    }
  }
  else if(currentSpeedPercent>target){
    currentSpeedPercent-=down*dt;
    if(currentSpeedPercent<target)
    {
      currentSpeedPercent=target;
    }
  }

  return;
}

void applyMotor() {
  int fwdDuty = 0;
  int revDuty = 0;

  if (currentSpeedPercent > 0.0f) {
    int maxDuty = (1 << MOTOR_RES) - 1;
    int minDuty = map(MOTOR_MIN_DUTY_PERCENT, 0, 100, 0, maxDuty);
    // Any non-zero speed (1-100%) is remapped onto minDuty..maxDuty, so the
    // "dead" low end of the PWM range is skipped entirely. 0% still means
    // fully off (handled by fwdDuty/revDuty defaulting to 0 above).
    int duty = map((int)round(currentSpeedPercent), 1, 100, minDuty, maxDuty);
    if (forward) {
      fwdDuty = duty;
    } else {
      revDuty = duty;
    }
  }
  // when powerOn is false (or speed is 0), both stay 0 - this is the
  // "gate to zero" behaviour carried over from the LED demo.

  ledcWrite(MOTOR_PLUS_PIN, fwdDuty);
  ledcWrite(MOTOR_MINUS_PIN, revDuty);

  // Debug LED shows speed magnitude, and only lights up at all if power
  // is actually on.
  int magnitudePercent = round(currentSpeedPercent);
  int ledDuty = map(magnitudePercent, 0, 100, 0, (1 << LED_RES) - 1);
  if (INVERT_DEBUG_LED) ledDuty = ((1 << LED_RES) - 1) - ledDuty;
  ledcWrite(DEBUG_LED_PIN, ledDuty);
}

// ---------- WEB PAGE (mobile-friendly) ----------
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
<title>Loco Controller</title>
<style>
  * { box-sizing: border-box; }
  /* Default theme = Southern Region (used until JS applies a saved choice) */
  :root, [data-theme="sr"] {
    --bg:            #0d2b1f;   /* dark malachite green - background */
    --primary:       #1b5e3c;   /* Southern Region malachite green */
    --primary-dark:  #0f3d28;   /* shadow/border green */
    --primary-bright:#2e8b57;   /* brighter green for active glow */
    --panel:         #f2e8d0;   /* SR coach cream panels */
    --panel-text:    #2a221a;   /* dark text on cream */
    --accent:        #c9a227;   /* "sunshine" lettering gold */
    --status-text:   #8fae9c;
    --sub-text:      #7a6b4d;
    --tick-text:     #cfead9;
  }
  [data-theme="gwr"] {
    --bg:            #241207;   /* dark chocolate brown - background */
    --primary:       #4a2c1a;   /* GWR chocolate brown */
    --primary-dark:  #2e1a0f;   /* shadow/border brown */
    --primary-bright:#6b4226;   /* brighter toffee brown for active glow */
    --panel:         #f5ecd7;   /* GWR coach cream panels */
    --panel-text:    #2e1a0f;   /* dark text on cream */
    --accent:        #d8a339;   /* GWR straw-gold lining */
    --status-text:   #b5a082;
    --sub-text:      #8a6a44;
    --tick-text:     #e8d3ae;
  }
  [data-theme="lner"] {
    --bg:            #0a1b33;   /* dark garter blue - background */
    --primary:       #12294d;   /* LNER garter blue */
    --primary-dark:  #081426;   /* shadow/border navy */
    --primary-bright:#1f4e8c;   /* brighter blue for active glow */
    --panel:         #eef1f5;   /* stainless-steel/teak-light panel */
    --panel-text:    #1a1a1a;   /* dark text on light panel */
    --accent:        #c8102e;   /* LNER lining scarlet */
    --status-text:   #9fb3c8;
    --sub-text:      #5c7692;
    --tick-text:     #cfe0f0;
  }
  body {
    margin: 0;
    font-family: -apple-system, Helvetica, Arial, sans-serif;
    background: var(--bg);
    color: var(--panel);
    display: flex;
    flex-direction: column;
    align-items: center;
    min-height: 100vh;
    padding: 24px 16px;
  }
  h1 {
    font-size: 1.4em;
    margin-bottom: 4px;
    color: var(--accent);
    letter-spacing: 1px;
  }
  .theme-row {
    display: flex;
    gap: 10px;
    margin-bottom: 16px;
  }
  .theme-swatch {
    width: 30px;
    height: 30px;
    border-radius: 50%;
    border: 2px solid rgba(255,255,255,0.25);
    padding: 0;
    cursor: pointer;
  }
  .theme-swatch.active {
    border-color: #fff;
    box-shadow: 0 0 0 2px rgba(255,255,255,0.4);
  }
  .status {
    color: var(--status-text);
    margin-bottom: 32px;
    font-size: 0.9em;
  }
  .power-btn {
    width: 140px;
    height: 140px;
    border-radius: 50%;
    border: none;
    font-size: 1.1em;
    font-weight: bold;
    letter-spacing: 1px;
    color: var(--panel);
    background: var(--primary);
    box-shadow: 0 0 0 4px var(--primary-dark) inset;
    transition: background 0.2s, box-shadow 0.2s;
  }
  .power-btn.on {
    background: var(--primary-bright);
    color: var(--panel-text);
    box-shadow: 0 0 24px 4px rgba(201,162,39,0.5), 0 0 0 4px var(--accent) inset;
  }
  .power-btn:active {
    transform: scale(0.97);
  }
  .power-btn:disabled {
    background: var(--primary-dark);
    color: #5c6b62;
    box-shadow: none;
    opacity: 0.6;
  }
  .card {
    width: 100%;
    max-width: 380px;
    margin-top: 40px;
    background: var(--panel);
    color: var(--panel-text);
    border-radius: 16px;
    padding: 20px;
    border: 2px solid var(--primary-dark);
  }
  .card label {
    display: flex;
    justify-content: space-between;
    font-size: 1em;
    margin-bottom: 12px;
  }
  #speedVal { color: var(--primary); font-weight: bold; }

  .reverser-row {
    display: flex;
    align-items: center;
    justify-content: space-between;
    margin-top: 24px;
  }
  .reverser-label {
    font-size: 1em;
  }
  .reverser-sublabel {
    font-size: 0.7em;
    color: var(--sub-text);
  }
  .reverser-switch {
    position: relative;
    width: 56px;
    height: 96px;
    background: var(--primary-dark);
    border: 2px solid var(--primary);
    border-radius: 28px;
    padding: 4px;
  }
  .reverser-switch.disabled {
    opacity: 0.4;
  }
  .reverser-knob {
    position: absolute;
    left: 4px;
    width: 44px;
    height: 44px;
    border-radius: 50%;
    background: var(--accent);
    transition: top 0.2s ease;
    display: flex;
    align-items: center;
    justify-content: center;
    font-size: 0.65em;
    font-weight: bold;
    color: var(--panel-text);
  }
  /* knob sits at the top for FORWARD, bottom for REVERSE */
  .reverser-knob.fwd { top: 4px; }
  .reverser-knob.rev { top: 44px; }
  .reverser-tick {
    position: absolute;
    left: 0;
    right: 0;
    text-align: center;
    font-size: 0.6em;
    color: var(--tick-text);
  }
  .reverser-tick.top { top: -18px; }
  .reverser-tick.bottom { bottom: -18px; }
  input[type=range] {
    width: 100%;
    height: 40px;
    -webkit-appearance: none;
    background: transparent;
  }
  input[type=range]::-webkit-slider-runnable-track {
    height: 10px;
    border-radius: 5px;
    background: var(--primary-dark);
  }
  input[type=range]::-webkit-slider-thumb {
    -webkit-appearance: none;
    width: 32px;
    height: 32px;
    margin-top: -11px;
    border-radius: 50%;
    background: var(--accent);
    border: 3px solid var(--primary);
  }
</style>
</head>
<body>
  <h1>Loco Controller</h1>
  <div class="theme-row" id="themeRow">
    <button class="theme-swatch" data-theme="sr"   style="background:#1b5e3c" onclick="setTheme('sr')"   title="Southern Region"></button>
    <button class="theme-swatch" data-theme="gwr"  style="background:#4a2c1a" onclick="setTheme('gwr')"  title="Great Western Railway"></button>
    <button class="theme-swatch" data-theme="lner" style="background:#12294d" onclick="setTheme('lner')" title="LNER"></button>
  </div>
  <div class="status" id="statusText">connecting...</div>

  <button class="power-btn" id="powerBtn" onclick="togglePower()" disabled>POWER</button>

  <div class="card">
    <label>Speed <span id="speedVal">0%</span></label>
    <input type="range" min="0" max="100" value="0" id="speedSlider"
           oninput="onSliderInput(this.value)" onchange="setSpeed(this.value)">

    <div class="reverser-row">
      <div>
        <div class="reverser-label">Reverser</div>
        <div class="reverser-sublabel" id="reverserState">FORWARD</div>
      </div>
      <div class="reverser-switch" id="reverserSwitch" onclick="toggleReverser()">
        <div class="reverser-tick top">FWD</div>
        <div class="reverser-knob fwd" id="reverserKnob"></div>
        <div class="reverser-tick bottom">REV</div>
      </div>
    </div>
  </div>

<script>
let power = false;
let speed = 0;
let forward = true;

function setTheme(theme) {
  document.body.setAttribute('data-theme', theme);
  document.querySelectorAll('.theme-swatch').forEach(btn => {
    btn.classList.toggle('active', btn.dataset.theme === theme);
  });
  try { localStorage.setItem('locoTheme', theme); } catch (e) {}
}

// Restore last-picked theme (falls back to Southern Region)
let savedTheme = 'sr';
try { savedTheme = localStorage.getItem('locoTheme') || 'sr'; } catch (e) {}
setTheme(savedTheme);

function refreshUI(data) {
  power = data.power;
  speed = data.speed;
  forward = data.forward;

  const btn = document.getElementById('powerBtn');
  btn.className = 'power-btn' + (power ? ' on' : '');
  btn.disabled = false;   // now that we know the real device state, allow control

  document.getElementById('speedSlider').value = speed;
  document.getElementById('speedVal').innerText = speed + '%';
  document.getElementById('statusText').innerText = power ? 'ON' : 'OFF';

  const knob = document.getElementById('reverserKnob');
  knob.className = 'reverser-knob ' + (forward ? 'fwd' : 'rev');
  document.getElementById('reverserState').innerText = forward ? 'FORWARD' : 'REVERSE';

  // Reverser can only be moved while stopped, matching a real loco lever
  const sw = document.getElementById('reverserSwitch');
  if (speed > 0) sw.classList.add('disabled');
  else sw.classList.remove('disabled');
}

function togglePower() {
  fetch('/power?state=' + (power ? 'off' : 'on'))
    .then(r => r.json())
    .then(refreshUI);
}

function onSliderInput(val) {
  document.getElementById('speedVal').innerText = val + '%';
}

function setSpeed(val) {
  fetch('/speed?value=' + val)
    .then(r => r.json())
    .then(refreshUI);
}

function toggleReverser() {
  if (speed > 0) return;   // locked while moving
  fetch('/direction?value=' + (forward ? 'reverse' : 'forward'))
    .then(r => r.json())
    .then(refreshUI);
}

function poll() {
  fetch('/status').then(r => r.json()).then(refreshUI).catch(()=>{
    document.getElementById('statusText').innerText = 'disconnected';
  });
}

poll();
setInterval(poll, 4000);
</script>
</body>
</html>
)rawliteral";

// ---------- HANDLERS ----------
void sendState() {
  String json = "{\"power\":" + String(powerOn ? "true" : "false") +
                ",\"speed\":" + String(targetSpeedPercent) +
                ",\"forward\":" + String(forward ? "true" : "false") + "}";
  server.send(200, "application/json", json);
}

void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void handlePower() {
  if (server.hasArg("state")) {
    String state = server.arg("state");
    powerOn = (state == "on");
    applyMotor();
  }
  sendState();
}

void handleSpeed() {
  if (server.hasArg("value")) {
    int val = server.arg("value").toInt();
    val = constrain(val, 0, 100);
    targetSpeedPercent = val;
    // Moving the slider only ever changes the remembered speed - it never
    // turns power on by itself. Only the POWER button gates output.
    applyMotor();
  }
  sendState();
}

void handleDirection() {
  if (server.hasArg("value")) {
    String val = server.arg("value");
    // Mirror a real loco reverser: it can only be moved while stopped,
    // to avoid a harsh instantaneous reversal under power.
    if (targetSpeedPercent == 0) {
      if (val == "forward") forward = true;
      else if (val == "reverse") forward = false;
      applyMotor();
    }
  }
  sendState();
}

void handleStatus() {
  sendState();
}

void handleNotFound() {
  server.send(404, "text/plain", "Not found");
}

// ---------- SETUP / LOOP ----------
void setup() {
  // Force all output pins to their safe/off state immediately, before PWM
  // is even attached, so there's no window where the motor could twitch
  // or the LED could glow during boot.
  pinMode(MOTOR_PLUS_PIN, OUTPUT);
  pinMode(MOTOR_MINUS_PIN, OUTPUT);
  pinMode(DEBUG_LED_PIN, OUTPUT);
  digitalWrite(MOTOR_PLUS_PIN, LOW);
  digitalWrite(MOTOR_MINUS_PIN, LOW);
  digitalWrite(DEBUG_LED_PIN, INVERT_DEBUG_LED ? HIGH : LOW);

  // PWM setup (Arduino-ESP32 core v3.x style)
  ledcAttach(MOTOR_PLUS_PIN, MOTOR_FREQ, MOTOR_RES);
  ledcAttach(MOTOR_MINUS_PIN, MOTOR_FREQ, MOTOR_RES);
  ledcAttach(DEBUG_LED_PIN, LED_FREQ, LED_RES);
  applyMotor();   // powerOn starts false, so this writes 0 everywhere

  // Setup Serial Port
  Serial.begin(115200);
  delay(200);

  // WiFi Access Point
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  IPAddress ip = WiFi.softAPIP();
  Serial.print("AP started. Connect to WiFi \"");
  Serial.print(AP_SSID);
  Serial.println("\" then browse to:");
  Serial.println(ip);

  // Web server routes
  server.on("/", handleRoot);
  server.on("/power", handlePower);
  server.on("/speed", handleSpeed);
  server.on("/direction", handleDirection);
  server.on("/status", handleStatus);
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("Web server started.");
}

void loop() {
  server.handleClient();
  updateMotorRamp();
  applyMotor();
}
