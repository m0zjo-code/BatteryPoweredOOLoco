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
    - SPEED slider from -100% (full reverse) to +100% (full forward),
      centred on 0% (stop), which is where it starts by default

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

const int MOTOR_PLUS_PIN  = 5;   // driver IN1 - PWM for forward
const int MOTOR_MINUS_PIN = 6;   // driver IN2 - PWM for reverse
const int DEBUG_LED_PIN   = 8;   // shows abs(speed) as brightness

const int MOTOR_FREQ = 20000;  // Hz - above audible range to avoid motor whine
const int MOTOR_RES  = 10;     // bits - 0-1023 steps for finer low-speed control

const int LED_FREQ = 5000;     // Hz - fine for an indicator LED
const int LED_RES  = 8;        // bits - 0-255 steps

// From earlier testing: this LED is wired "inverted" (sinking), so 0 duty
// must mean full HIGH on the pin, not full LOW. Flip if you rewire it.
const bool INVERT_DEBUG_LED = true;

// ---------- STATE ----------
bool powerOn = false;     // starts OFF - loco disabled by default
int  speedPercent = 0;    // -100 (full reverse) .. 0 (stop) .. +100 (full forward)
                           // remembered even when power is off

WebServer server(80);

// ---------- APPLY OUTPUT ----------
void applyMotor() {
  int fwdDuty = 0;
  int revDuty = 0;

  if (powerOn) {
    if (speedPercent > 0) {
      fwdDuty = map(speedPercent, 0, 100, 0, (1 << MOTOR_RES) - 1);
    } else if (speedPercent < 0) {
      revDuty = map(-speedPercent, 0, 100, 0, (1 << MOTOR_RES) - 1);
    }
  }
  // when powerOn is false, both stay 0 regardless of speedPercent -
  // this is the "gate to zero" behaviour carried over from the LED demo.

  ledcWrite(MOTOR_PLUS_PIN, fwdDuty);
  ledcWrite(MOTOR_MINUS_PIN, revDuty);

  // Debug LED shows magnitude only, regardless of direction, and only
  // lights up at all if power is actually on.
  int magnitudePercent = powerOn ? abs(speedPercent) : 0;
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
  body {
    margin: 0;
    font-family: -apple-system, Helvetica, Arial, sans-serif;
    background: #101418;
    color: #eee;
    display: flex;
    flex-direction: column;
    align-items: center;
    min-height: 100vh;
    padding: 24px 16px;
  }
  h1 {
    font-size: 1.4em;
    margin-bottom: 4px;
  }
  .status {
    color: #888;
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
    color: #fff;
    background: #333;
    box-shadow: 0 0 0 4px #222 inset;
    transition: background 0.2s, box-shadow 0.2s;
  }
  .power-btn.on {
    background: #1fae4b;
    box-shadow: 0 0 24px 4px rgba(31,174,75,0.6);
  }
  .power-btn:active {
    transform: scale(0.97);
  }
  .power-btn:disabled {
    background: #222;
    color: #555;
    box-shadow: none;
    opacity: 0.6;
  }
  .card {
    width: 100%;
    max-width: 380px;
    margin-top: 40px;
    background: #181d24;
    border-radius: 16px;
    padding: 20px;
  }
  .card label {
    display: flex;
    justify-content: space-between;
    font-size: 1em;
    margin-bottom: 12px;
  }
  #speedVal { color: #1fae4b; font-weight: bold; }
  .direction-labels {
    display: flex;
    justify-content: space-between;
    font-size: 0.75em;
    color: #666;
    margin-top: 6px;
  }
  input[type=range] {
    width: 100%;
    height: 40px;
    -webkit-appearance: none;
    background: transparent;
  }
  input[type=range]::-webkit-slider-runnable-track {
    height: 10px;
    border-radius: 5px;
    background: #333;
  }
  input[type=range]::-webkit-slider-thumb {
    -webkit-appearance: none;
    width: 32px;
    height: 32px;
    margin-top: -11px;
    border-radius: 50%;
    background: #1fae4b;
    border: 3px solid #eee;
  }
</style>
</head>
<body>
  <h1>Loco Controller</h1>
  <div class="status" id="statusText">connecting...</div>

  <button class="power-btn" id="powerBtn" onclick="togglePower()" disabled>POWER</button>

  <div class="card">
    <label>Speed <span id="speedVal">0%</span></label>
    <input type="range" min="-100" max="100" value="0" id="speedSlider"
           oninput="onSliderInput(this.value)" onchange="setSpeed(this.value)">
    <div class="direction-labels"><span>REVERSE</span><span>STOP</span><span>FORWARD</span></div>
  </div>

<script>
let power = false;

function describeSpeed(val) {
  val = parseInt(val);
  if (val === 0) return '0%';
  return (val > 0 ? '+' : '') + val + '% ' + (val > 0 ? 'FWD' : 'REV');
}

function refreshUI(data) {
  power = data.power;
  const btn = document.getElementById('powerBtn');
  btn.className = 'power-btn' + (power ? ' on' : '');
  btn.disabled = false;   // now that we know the real device state, allow control
  document.getElementById('speedSlider').value = data.speed;
  document.getElementById('speedVal').innerText = describeSpeed(data.speed);
  document.getElementById('statusText').innerText = power ? 'ON' : 'OFF';
}

function togglePower() {
  fetch('/power?state=' + (power ? 'off' : 'on'))
    .then(r => r.json())
    .then(refreshUI);
}

function onSliderInput(val) {
  document.getElementById('speedVal').innerText = describeSpeed(val);
}

function setSpeed(val) {
  fetch('/speed?value=' + val)
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
                ",\"speed\":" + String(speedPercent) + "}";
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
    val = constrain(val, -100, 100);
    speedPercent = val;
    // Moving the slider only ever changes the remembered speed - it never
    // turns power on by itself. Only the POWER button gates output.
    applyMotor();
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
  Serial.begin(115200);
  delay(200);

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
  server.on("/status", handleStatus);
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("Web server started.");
}

void loop() {
  server.handleClient();
}
