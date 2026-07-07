# 🔌 Retrofit Switch Controller

**A little robot arm that flips your wall switches for you.**

No rewiring. No replacing your switches with "smart" ones. Just a small device that sits over your existing switch plate, learns where each switch is, and presses it on command — from your phone, or on a schedule you set.

Think of it as teaching a robot finger to do what your finger does, except it never forgets to turn the lights off.

<p align="center">
  <em>🖐️ Teach it your switches → 📱 Control from a web app → ⏰ Automate the rest</em>
</p>

### Why this exists
Smart switches mean rewiring your walls. Smart bulbs mean replacing every bulb. This project skips all of that — it retrofits automation onto switches you already have, with a tiny arm and a web page hosted right on the device itself.

### What it can do
- 👆 **Learns your switches** — just jog the arm to each switch by hand once, hit save, done
- 📱 **Control from any browser** — no app install, connects straight to its own WiFi
- ⏰ **Set it and forget it** — schedule switches daily, on specific days, or for a one-off time
- 🏠 **Works standalone** — has its own WiFi hotspot, and can also join your home network

---

## 🚀 Getting Started

### What you'll need
- [PlatformIO](https://platformio.org/) (VS Code extension or CLI)
- An ESP32 dev board
- A USB cable

### Flash it
```bash
git clone <your-repo-url>
cd retrofit-switch

pio run              # build the firmware
pio run -t upload    # flash it to the board
pio run -t uploadfs  # upload the web UI files
```

### First boot
1. The device starts its own WiFi hotspot: **`Retrofit Switch`** (password: `12345678`)
2. Connect to it and open `192.168.4.1` in a browser (or `retroswitch.local` if it's also joined your home WiFi)
3. Go to the **Switches** tab, jog the arm over to each switch, and save its ON/OFF position
4. If the arm hasn't found its reference position yet, use **Settings → Force Home**

### Using the web app
- **Switches** — jog the arm, teach positions, test each switch with one tap
- **Schedules** — set up daily, day-of-week, or one-time automation
- **Settings** — WiFi setup, homing, and diagnostics

---

## 🗺️ Roadmap

- [ ] Resolve TMC2209 UART communication (see Technical Details below)
- [ ] Re-enable StallGuard-based sensorless homing
- [ ] Host the web app fully on-device to avoid mixed-content issues from the earlier externally-hosted frontend
- [ ] Add per-switch calibration drift compensation
- [ ] Support switch plate layouts beyond 5-gang Quincunx

---

## 🧠 Technical Details

### Hardware

| Component | Detail |
|---|---|
| MCU | ESP32 (Arduino framework, PlatformIO) |
| Stepper drivers | 2× TMC2209 (UART-configurable) |
| Motors | 2× NEMA 17, 1.8°/step, 16x microstepping (3200 steps/rev) |
| Actuator | JF-0530B solenoid (intermittent duty only) |
| Mechanism | 5-bar parallel linkage — L1 = L2 = 50mm, L3 = L4 = 75mm, motor separation D = 47.7mm |
| Switch layout | 5-gang Quincunx switch plate |
| PCB | Custom board, designed in Altium |

### Pin Mapping

| Signal | GPIO | Notes |
|---|---|---|
| Motor 1 (U3) DIR | 4 | |
| Motor 1 STEP | 16 | |
| Motor 1 EN | 17 | Active LOW |
| Motor 1 DIAG | 21 | StallGuard, interrupt-driven |
| Motor 1 PDN_UART | 22 | |
| Motor 2 (U5) DIR | 12 | |
| Motor 2 STEP | 27 | |
| Motor 2 EN | 18 | Active LOW |
| Motor 2 DIAG | 34 | Input-only, polled (no interrupt support) |
| Motor 2 PDN_UART | 25 | |
| Solenoid | 13 | MOSFET gate |

### ⚠️ Current Blocker: TMC2209 UART

- **UART to both TMC2209 drivers is non-functional** (`IFCNT` reads 0 / connection tests return `FAILED` on both drivers). This means StallGuard parameters (`SGTHRS`, `TCOOLTHRS`) can't be configured, so sensorless homing can't run.
- Suspected root cause: PCB-level issues — missing UART split resistor and/or PDN_UART soldering defects on U3/U5.
- **Current workaround:** the system runs in **standalone mode** with potentiometer-based current control, plus a **force-home bypass** that lets you manually set a reference position to unblock movement without StallGuard.
- Until UART is restored, homing accuracy depends on the manual force-home reference rather than automatic stall detection.

**Fix checklist (from schematic review):**
- [ ] Add 1kΩ series resistor on ESP32 TX before joining PDN_UART (half-duplex split)
- [ ] Set MS1/MS2 pins to explicit levels (10kΩ to VCC/GND, don't leave floating)
- [ ] Add 100nF ceramic decoupling caps on VM pins alongside existing electrolytics
- [ ] Verify DIAG/INDEX pins aren't floating
- [ ] Re-check PDN_UART solder joints on U3 and U5

---

## 📄 License

_Add your license here (e.g. MIT)._
