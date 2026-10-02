# EZ-Steer Assist Level Protocol

**What this is:** reverse-engineered documentation of the signal SuperATV's Adjustable
Power Steering Assist Controller sends to an EZ-Steer EPS module, plus an Arduino
sketch that replaces that controller with a knob or a fixed setting.

**Who it's for:** anyone running one of these EPS columns who wants adjustable assist
without buying the OEM controller, or who wants to drive it from their own electronics
(vehicle speed, a switch panel, a dash). If you just want the answer, it's the
[frequency table](#assist-level-table) below.

**Status:** measured and verified. Built and running on a Miata track car.

## The hardware this applies to

The SuperATV EZ-Steer 220W universal / sprint car column. The same unit is sold as:

- DCE Microsteer EPAS200
- Rugged / Demon PEPS-9001
- Tusk
- Joes Racing

OEM appears to be Tianjin Deco. If your module has a 3-wire connector carrying
remote-on, an LED output and one signal wire, this probably applies to you.

## The protocol

The assist level is carried by the **frequency** of a square wave on the single
signal wire. Not voltage, not duty cycle.

- **Duty is always ~50%** and carries no information. The OEM controller's low time
  is a fixed 7.7 µs longer than its high time at every setting, from a 529 µs period
  to a 35 ms one — that's software overhead in one branch of its toggle loop, nothing
  more.
- **5 V logic levels.** The module's input goes through a 5.1 kΩ resistor into an
  LM2903 comparator, so it only cares whether each edge crosses roughly 2.7 V.
- **100% assist is the line held static high** — electrically identical to no
  controller attached, which is why an unplugged module gives full assist.
- **A steady DC voltage does nothing.** The module ignores it and falls back to full
  assist. This is the trap: a multimeter on the OEM controller's output reads a
  plausible-looking 1.74–2.35 V that rises with the setting, which sends you down a
  dead end for a long time. That reading is the average of the square wave.

### Assist level table

| Setting | Frequency | Setting | Frequency |
| --- | --- | --- | --- |
| 0% | 1888.7 Hz | 55% | 644.0 Hz |
| 5% | 1889.0 Hz | 60% | 546.3 Hz |
| 10% | 1857.5 Hz | 65% | 458.0 Hz |
| 15% | 1737.8 Hz | 70% | 373.9 Hz |
| 20% | 1613.2 Hz | 75% | 297.2 Hz |
| 25% | 1464.6 Hz | 80% | 223.9 Hz |
| 30% | 1303.4 Hz | 85% | 156.3 Hz |
| 35% | 1148.4 Hz | 90% | 91.1 Hz |
| 40% | 1004.6 Hz | 95% | 28.1 Hz |
| 45% | 871.0 Hz | 100% | static high |
| 50% | 751.6 Hz | | |

Every value measured from a real controller with a logic analyzer at 16 or 24 MHz.
Note that 0% and 5% produce the same output — the controller's floor is 1889 Hz.

The module accepts a wide band around each value, so clock accuracy is not critical.
Adjacent steps differ by 4–20% in frequency; an Arduino Nano Every's internal
oscillator (±1%) is a fraction of one step. Measured error on the build below was
+0.36% at both 50% and 95%.

## Failure behavior

Loss of signal — broken wire, dead controller, unpowered board — leaves the line
static, which the module reads as no controller and gives **full assist**. Decide
whether that's acceptable for your car before you rely on this.

## The build

Arduino Nano Every, a 200 kΩ linear pot, three wires to the EPS module which uses a 4 pin KET connector.

| Nano Every | Connects to |
| --- | --- |
| VIN | switched 12 V (the EPS remote-on wire) |
| GND | EPS module ground terminal, and pot pin 1 |
| D12 | EPS assist signal wire |
| 5V | pot pin 3 |
| A0 | pot pin 2 (wiper) |

Optional 0.1 µF from A0 to ground if the reading flickers.

Ground at the module's own ground terminal, not a chassis point — the module's ground
carries the motor's 30 A and shared copper puts that current into your signal
reference.

`eps_assist_knob.ino` gives 21 detented steps matching the OEM controller's 5%
increments, with a serial readout of the current setting and the frequency being
generated. It drives TCA0 directly and will not run on an Uno. For a fixed assist
level, delete the pot and hard-code one table entry.

### Serial commands

115200 baud, Newline line ending.

| Command | Does |
| --- | --- |
| `?` | report current step and frequency |
| `t` | print the whole table |
| `k` | return to knob control |
| `s45` | force a setting in percent |
| `f900` | force a raw frequency in Hz |
| `v` | continuous reporting while the knob moves |

## How this was measured

A logic analyzer on the OEM controller's signal wire, one capture per setting at
16–24 MHz, decoded for period and duty.

Two things to watch if you repeat this:

- **Sample fast enough.** A 20 kHz capture of a 1.5 kHz signal produced convincing
  but entirely false structure — apparent narrow pulses at a frequency that didn't
  exist. 16 MHz or better.
- **Probe ground matters.** Half the captures read ~99% duty with a narrow low pulse
  instead of the real 50%. Same setting, same channel, different capture. Frequency
  was unaffected and reproduced to 0.06%, but duty readings were not trustworthy.

Raw captures are in `captures/` if you want to verify.

## License

MIT. No warranty — this drives a steering system. Test it on stands before you drive it.
