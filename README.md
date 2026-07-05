# BatteryPoweredOOLoco

A battery-powered, WiFi-controlled conversion for OO gauge model locomotives. An ESP32 hosts its own WiFi access point and a mobile-friendly web control page, driving the loco's motor through an H-bridge — no track power or DCC required. All the electronics live inside a 3D-printed tender that replaces (or sits alongside) the stock tender shell.

**Current test platform:** an old Hornby Battle of Britain class loco + tender, used as the development mule while the tender internals and mount are dialed in.

## How it works

- The ESP32 boots into access-point mode and serves a single-page speed controller over WiFi — no app or internet connection needed, just connect a phone or laptop to the ESP32's own network and open the page in a browser.
- The page has a POWER button (loco is disabled until you turn it on, and stays disabled by default at boot) and a speed slider running from -100% (full reverse) through 0% (stop) to +100% (full forward).
- The ESP32 drives the motor via two PWM outputs into a TC1508A-style dual H-bridge, one pin per direction, so only one side is ever active at a time.
- A status LED gives an at-a-glance "how fast" readout, showing the magnitude of the current speed regardless of direction — handy for testing without needing to watch the phone screen.
- All motor outputs are forced to a safe/off state before anything else happens at boot, so there's no risk of the loco twitching or lurching during power-up.

## Repo contents

| File | Purpose |
|---|---|
| `ESP32_Loco_Controller_1.ino` | Arduino sketch for the ESP32: WiFi AP, web UI, and bidirectional PWM motor control. |
| `TenderBody.scad` | 3D-printable tender shell that houses the electronics. Tapered box profile matching the loco's tender outline, with a lidded coal-hatch opening on top, an internal mezzanine floor to carry the electronics tray, and a wiring pass-through hole down to the chassis. |
| `MatingPlate.scad` | Interface plate that mates the tender shell to the underlying tender chassis/platform, with a locating hole for alignment/fixing. |
| `ControlElectronicsMount.scad` | Small mounting tray with PCB edge-clip holders for the motor driver board, the ESP32, and the voltage regulator, sized to sit inside the tender on the mezzanine floor. |

## Hardware

- **MCU:** ESP32 (classic WROOM/WROVER or S2/S3/C3 — see note below on GPIO 8)
- **Motor driver:** TC1508A-style dual H-bridge driver (or equivalent), driven with two independent PWM signals rather than a PWM+direction pair
- **Power:** onboard battery (not track power) feeding the ESP32 and driver through a voltage regulator
- **Chassis:** currently a Hornby Battle of Britain class OO gauge loco/tender, used as the test bed

### Pin configuration (as shipped in the sketch)

| Signal | GPIO | Notes |
|---|---|---|
| Motor + (forward PWM) | 5 | → driver IN1 |
| Motor − (reverse PWM) | 6 | → driver IN2 |
| Debug/status LED | 8 | **Not available as GPIO on classic ESP32-WROOM/WROVER** (reserved for SPI flash) — fine on S2/S3/C3. Change `DEBUG_LED_PIN` to suit your board. |

Motor PWM runs at 20 kHz (above the audible range, to avoid motor whine) with 10-bit resolution for finer control at low speeds. The status LED runs at 5 kHz, 8-bit, and is wired "inverted" (sinking) in the current build — flip `INVERT_DEBUG_LED` if you rewire it.

H-bridge truth table used by the sketch:

| IN1 | IN2 | Result |
|---|---|---|
| LOW | LOW | Standby / coast |
| PWM | LOW | Forward, speed = IN1 duty |
| LOW | PWM | Reverse, speed = IN2 duty |
| HIGH | HIGH | Brake (not currently used) |

## 3D-printed parts

All models are parametric OpenSCAD files, dimensioned in mm.

- **TenderBody.scad** — tapered box (35 mm bottom width tapering to 31 mm top width, 86 mm long, ~21 mm tall, 2 mm walls) with a solid mezzanine floor partway up to carry the electronics tray, an open main cavity above it, a wiring hole through the mezzanine floor, and a coal-hatch-style opening cut into the top/roof for access.
- **MatingPlate.scad** — a thin (5 mm) interface plate that locates and secures the tender body onto the chassis/underframe, with a floor recess sized to the tender's footprint and a locating hole for fixing.
- **ControlElectronicsMount.scad** — a small tray with edge-clip (U-channel) holders sized for the motor driver PCB, the ESP32 module, and the voltage regulator board, so all three sit in fixed positions on the mezzanine floor inside the tender.

Print settings aren't finalized yet — treat wall thicknesses and clearances in the SCAD files as a starting point and adjust to your printer/tolerances.

## Using it

1. Flash `ESP32_Loco_Controller_1.ino` to the ESP32 (Arduino-ESP32 core v3.x, using the `ledcAttach`-style PWM API).
2. Wire the ESP32 to the H-bridge driver per the pin table above, and the driver's motor output to the loco's motor.
3. Print and assemble `TenderBody.scad`, `ControlElectronicsMount.scad`, and `MatingPlate.scad`, and fit the electronics inside the tender.
4. Power on the loco. On your phone or laptop, connect to the WiFi network `ESP32-Controller` (password `controller123` — change this in the sketch before relying on it for anything beyond bench testing).
5. Browse to the IP address printed over serial at boot (also visible as the ESP32's AP gateway address, typically `192.168.4.1`).
6. Tap POWER to enable the motor outputs, then use the slider to control speed and direction.

## Status / known limitations

- Actively in testing on a Hornby Battle of Britain class loco — fit, clearances, and wiring routing are still being refined.
- WiFi credentials are hardcoded in the sketch; change them before use beyond the bench.
- No captive/soft-start ramping on speed changes yet — the slider applies duty directly.
- Brake mode (`IN1`/`IN2` both HIGH) is wired into the driver truth table but not currently used by the firmware.
