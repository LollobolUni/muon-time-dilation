"""Record a timer test with the pulse generator.

Requires Python 3 and pyserial (pip install pyserial). Wiring: GP2 of the generator to GP2 of the timer
(and GP3 to GP3 to test the alignment between channels), a common ground, both Picos connected to the PC
over USB.

Usage:
    python record_timer_test.py d      # pairs at known distances
    python record_timer_test.py e      # exponential delays (mean 2.197 us)
    python record_timer_test.py 1      # 1 kHz load test (also 2 = 10 kHz, 3 = 100 kHz)

With a single USB cable (generator powered by the timer, running by itself):
    python record_timer_test.py standalone d 60    # generator mode, seconds

The script finds which port is the timer and which is the generator (the timer prints "# timer", the
generator answers the h command). It writes two files in a timer-tests folder next to this script (or in
the folder given with --out): what the generator sent and what the timer recorded.
"""

import sys
import time
from datetime import datetime
from pathlib import Path

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    sys.exit("pyserial is missing: pip install pyserial")

VID_PICO = 0x2E8A
OUT = Path(__file__).resolve().parent / "timer-tests"


def find_ports():
    ports = [p.device for p in list_ports.comports() if p.vid == VID_PICO]
    if len(ports) < 2:
        sys.exit(f"Found {len(ports)} Pico on the serial ports; 2 are needed (timer and generator).")
    timer = generator = None
    for name in ports:
        s = serial.Serial(name, 115200, timeout=0.2)
        s.dtr = True
        time.sleep(1.5)
        s.write(b"h")
        text = b""
        end = time.time() + 2.0
        while time.time() < end:
            text += s.read(4096)
        s.close()
        if b"# timer" in text:
            timer = name
        elif b"# pulse-generator" in text:
            generator = name
    if not timer or not generator:
        sys.exit(f"Ports not recognised: timer {timer}, generator {generator}. Reload the .uf2 files?")
    return timer, generator


def timer_only(seconds: float, label: str):
    """Single USB cable: record the timer for a given time, with the generator running by itself."""
    ports = [p.device for p in list_ports.comports() if p.vid == VID_PICO]
    if not ports:
        sys.exit("No Pico on the serial ports.")
    if len(ports) > 1:
        sys.exit(f"More than one Pico connected ({ports}): in this mode only the timer must be connected.")
    OUT.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now().strftime("%Y-%m-%d_%H%M%S")
    f_t = OUT / f"{stamp}_{label}_timer.txt"
    st = serial.Serial(ports[0], 115200, timeout=0.05)
    st.dtr = True
    end = time.time() + seconds
    with open(f_t, "wb") as out:
        out.write(f"# recorded on {stamp}, port {ports[0]}, timer only, {seconds:g} s, "
                  f"generator standalone in mode {label}\n".encode())
        while time.time() < end:
            out.write(st.read(65536))
    st.close()
    n_t = sum(1 for r in f_t.read_text(errors="replace").splitlines() if r[:1].isdigit())
    if b"# timer" not in f_t.read_bytes():
        print("Warning: the file has no timer header. Is it the right Pico?")
    print(f"timer: {n_t} events in {seconds:g} s -> {f_t}")
    if n_t == 0:
        print("No events: check the GP2-GP2 wire, the ground, and that the generator LED is blinking.")


def main():
    global OUT
    args = sys.argv[1:]
    if "--out" in args:
        i = args.index("--out")
        OUT = Path(args[i + 1])
        del args[i:i + 2]
    if args and args[0] == "standalone":
        label = args[1] if len(args) > 1 else "d"
        seconds = float(args[2]) if len(args) > 2 else 60.0
        return timer_only(seconds, label)
    mode = args[0] if args else "d"
    if mode not in "de123" or len(mode) != 1:
        sys.exit("Mode: d, e, 1, 2 or 3")
    timer, gen = find_ports()
    print(f"timer on {timer}, generator on {gen}")
    OUT.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now().strftime("%Y-%m-%d_%H%M%S")
    f_t = OUT / f"{stamp}_{mode}_timer.txt"
    f_g = OUT / f"{stamp}_{mode}_generator.txt"
    st = serial.Serial(timer, 115200, timeout=0.05)
    sg = serial.Serial(gen, 115200, timeout=0.05)
    st.dtr = sg.dtr = True
    time.sleep(1.0)
    st.reset_input_buffer()
    sg.reset_input_buffer()
    with open(f_t, "wb") as out_t, open(f_g, "wb") as out_g:
        out_t.write(f"# recorded on {stamp}, port {timer}\n".encode())
        out_g.write(f"# recorded on {stamp}, port {gen}\n".encode())
        sg.write(mode.encode())
        finished = None
        while True:
            dt = st.read(65536)
            dg = sg.read(65536)
            out_t.write(dt)
            out_g.write(dg)
            if b"# end" in dg and finished is None:
                finished = time.time()
            if finished and time.time() - finished > 2.0:   # last timer events
                break
    st.close()
    sg.close()
    n_t = sum(1 for r in f_t.read_text(errors="replace").splitlines() if r[:1].isdigit())
    n_g = sum(1 for r in f_g.read_text(errors="replace").splitlines() if r.startswith("g,"))
    print(f"generator: {n_g} pulses -> {f_g}")
    print(f"timer: {n_t} events -> {f_t}")


if __name__ == "__main__":
    main()
