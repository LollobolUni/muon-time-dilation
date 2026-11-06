# Cosmic muons and the stretching of time

A self-built experiment to measure the lifetime of cosmic-ray muons and to show, with data taken from cable cars in the Alps, that moving clocks run slow.

> [!TIP]
> New to the project? The [project overview (PDF)](docs/muon-time-dilation_project-overview.pdf) is a good place to start: in ten illustrated pages it explains the physics, how the detector works and what comes next.

Muons are created about 15 km above the ground and live on average 2.2 µs. Without special relativity almost none of them would reach the surface. The experiment has two parts:

1. **Muon lifetime at rest.** A one-litre plastic scintillator stops about a thousand muons a day. The time between the flash of the stopping muon and the flash of its decay electron gives the lifetime, with a target precision of 2–3%.
2. **Muon flux versus altitude.** Two small scintillator tiles in coincidence, with lead in between, count muons at 5–6 stops from Milan up to about 3000 m. Without time dilation the flux at 3000 m would be about 95 times that on the plain; with relativity, less than twice.

## Status

Design stage (November 2026). The electronics, firmware and mechanics are designed; the timer firmware has been tested on real hardware. Assembly and the first measurements are next.

## Repository structure

| Folder | Contents |
|---|---|
| `docs/` | Project overview (PDF) |
| `hardware/kicad/` | KiCad projects of the two circuit boards, with the project library |
| `hardware/schematics/` | Schematics and board layouts as PDF |
| `hardware/gerbers/` | Manufacturing files for the two boards |
| `hardware/bom/` | Bills of materials, one per board |
| `mechanics/parts/` | Parts to 3D-print, as STEP files |
| `mechanics/assemblies/` | Complete assemblies of the block detector and of the tile module |
| `mechanics/drawings/` | Drawings of the threaded inserts for the two enclosures |
| `firmware/timer/` | Four-channel timer for the Raspberry Pi Pico 2 |
| `firmware/pulse-generator/` | Test pulse generator for the timer (Raspberry Pi Pico 2) |
| `firmware/tools/` | Script to record a timer test from the PC |

## Hardware

| Board | Size | Function |
|---|---|---|
| PCB-401, channel board | 40 × 40 mm | One SiPM (onsemi MicroFC-60035) with bias filter, pulse shaping, amplifier (OPA356) and fast comparator (TLV3501). One board per SiPM; the same board fits the block and the tiles. |
| PCB-402, base board | 100 × 64 mm | Raspberry Pi Pico 2 W timer, SiPM bias supply (MAX5026, 22.1–28.2 V), analogue 3.3 V, four threshold outputs, four channel connectors, pressure sensor and microSD headers. |

- The KiCad projects are saved with KiCad 10. `PCB-402` uses the project library stored in the `PCB-401` folder, so keep the two folders side by side.
- Channel cables are straight six-way JST PH cables, pin 1 to pin 1: 1 HV, 2 GND, 3 THR, 4 +3.3 V analogue, 5 GND, 6 OUT.
- The SiPM centre paddle (pin 5) is left unsoldered.
- The bill of materials lists manufacturer and LCSC part numbers. Where the original part was not available, the substitute is noted in the `Note` column.

## Mechanics

The enclosures are designed for SLA printing in resin. Parts `3DP-301` (block enclosure) and `3DP-311` (tile enclosure, two copies) take four M3 × 6.25 mm threaded inserts each, as shown in the drawings. All parts are oriented with Z up and the opening towards +Z.

| Part | Quantity |
|---|---|
| `3DP-301` Block enclosure | 1 |
| `3DP-302` Block lid | 1 |
| `3DP-303` SiPM holder, block | 2 |
| `3DP-304` Spring caps (four joined) | 1 |
| `3DP-311` Tile enclosure | 2 |
| `3DP-312` Tile lid | 2 |
| `3DP-313` SiPM holder, tile | 2 |

## Firmware

The timer runs on a Raspberry Pi Pico 2 (the base board also accepts the Pico 2 W, which has the same pinout). It timestamps rising edges on GP2–GP5 with a step of two system-clock cycles (13.3 ns at 150 MHz), using one PIO state machine per channel and DMA. Events are sent over USB serial as `channel,time_in_steps`; lines starting with `#` are header and status lines.

Ready-to-flash `.uf2` files are included: hold BOOTSEL, connect the Pico and copy the file onto the drive that appears. To build from source with the Raspberry Pi Pico SDK (2.x):

```
cd firmware/timer
mkdir build && cd build
cmake .. -DPICO_SDK_PATH=/path/to/pico-sdk
make
```

The pulse generator produces pairs of pulses at known distances or with exponentially distributed delays (mean 2.197 µs), to test the timer before the detector is built. `firmware/tools/record_timer_test.py` records a test from both boards (requires `pyserial`).

## Author

Lorenzo Bologna, Physics Engineering, Politecnico di Milano.
