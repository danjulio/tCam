# Building tCam-Mini firmware on Windows

Step-by-step setup for a fresh Windows 10/11 machine.  The firmware requires
**ESP-IDF v4.4.4** — newer IDF versions (v5.x) will not build this project.

Target hardware: ESP32-WROVER-E, 8 MB flash, CP2102N USB-serial bridge.


## 1. Install the USB-serial driver

The board's USB bridge is a Silicon Labs CP2102N.  Windows 11 usually installs a
driver automatically, but the vendor driver is more reliable.

Download and install the **CP210x Universal Windows Driver** from Silicon Labs:
https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers

Plug the camera in, open Device Manager, and look under **Ports (COM & LPT)**.
You should see `Silicon Labs CP210x USB to UART Bridge (COMn)`.  Note the COM
number — you need it to flash.


## 2. Install ESP-IDF v4.4.4

Download the **offline installer for v4.4.4** from
https://dl.espressif.com/dl/esp-idf/

Choose the entry labelled *ESP-IDF v4.4.4 — Offline Installer* (roughly 1 GB).
The offline installer bundles Python, the Xtensa toolchain, OpenOCD and Git, so
nothing else has to be installed separately.

When the installer runs:

- **Installation directory: `C:\esp`** — do not accept the default under your
  user profile.  Windows has a 260-character path limit and the ESP32 build tree
  is deeply nested; a long install path produces `Filename too long` errors part
  way through the build.  Keep it short.
- Let it install its own **Python** and **Git** unless you already have them.
- Tick the checkbox to **register the ESP-IDF PowerShell/CMD shortcuts** in the
  Start menu.  That shortcut is how you open a working build shell.

Installation takes 10–20 minutes.


## 3. Get the source

Open the Start menu shortcut **ESP-IDF 4.4 PowerShell** (see step 4 for why this
matters) and clone the repository somewhere with a short path:

```
cd C:\
git clone https://github.com/McGeaverBeaver/tCam.git tcam
```

That gives you `C:\tcam`.


## 4. Open a build shell — every time

**The ESP-IDF environment is per-shell and does not persist.**  Closing the
PowerShell window discards it; a fresh window knows nothing about `idf.py`.  This
is the single most common reason for `idf.py : The term 'idf.py' is not
recognized`.

Two ways to get a working shell:

- Start menu → **ESP-IDF 4.4 PowerShell** (or *ESP-IDF 4.4 CMD*), or
- open any PowerShell and run the activation script:

```
C:\esp\esp-idf\export.ps1
```

Verify with `idf.py --version`.  Note: on a Windows install this often prints
something like `ESP-IDF v1.0.3` — that is the version of the small `idf.py`
launcher executable, not the IDF itself, and it is harmless.  To confirm the real
IDF version, run `git -C C:\esp\esp-idf describe --tags`, which should report
`v4.4.4`.


## 5. Fix the Python `pkg_resources` error

ESP-IDF v4.4.4 predates a breaking change in Python packaging: `setuptools` 81
removed `pkg_resources`, which the IDF build scripts import.  On a new install
you will hit:

```
ModuleNotFoundError: No module named 'pkg_resources'
```

Fix it once, inside an activated ESP-IDF shell:

```
python -m pip install "setuptools<81"
```

Do not install a bare `setuptools` — pip will pull the newest version and the
error returns.  The version pin is the fix.


## 6. Build

**Watch the directory.**  This repository contains two separate projects:

| Directory | Project | Flash |
|---|---|---|
| `C:\tcam\tCam-Mini\firmware` | **tCam-Mini — this is the one you want** | 8 MB |
| `C:\tcam\tCam\firmware` | tCam handheld (different product) | 16 MB |

Windows paths are case-insensitive, so `cd C:\tcam\tcam` silently lands you in
the **handheld** project.  Building and flashing that produces an image with a
16 MB flash header, which the 8 MB Mini rejects at boot with
`Detected size(8192k) smaller than the size in the binary image header(16384k)`
followed by an endless reboot loop.

The safe habit is to never `cd`, and instead pass the project directory
explicitly with `-C`:

```
idf.py -C C:\tcam\tCam-Mini\firmware build
```

The first build takes several minutes; later builds are incremental.  It ends
with a summary of the binary size and the `esptool.py write_flash` arguments.


## 7. Flash

With the camera plugged in and the COM port from step 1 (`COM10` in this
example):

```
idf.py -C C:\tcam\tCam-Mini\firmware -p COM10 -b 921600 flash monitor
```

`monitor` attaches the serial console at 115200 baud afterwards so you can watch
it boot.  Press **Ctrl+]** to exit the monitor.

If flashing fails to start, hold the board's **Boot** button while the tool says
`Connecting....`, or lower the speed with `-b 115200`.


## 8. If the camera will not boot

If the board was ever flashed with the wrong project, or a previous flash was
interrupted, erase it completely and reflash:

```
idf.py -C C:\tcam\tCam-Mini\firmware -p COM10 erase-flash
idf.py -C C:\tcam\tCam-Mini\firmware -p COM10 -b 921600 flash monitor
```

Erasing also clears saved WiFi credentials and the TLS certificate, so the camera
comes back up broadcasting its own access point and generates a fresh
certificate on first boot.


## Quick reference

```
# once per machine
install CP210x driver
install ESP-IDF v4.4.4 offline installer to C:\esp
python -m pip install "setuptools<81"     (inside an ESP-IDF shell)

# once per PowerShell window
C:\esp\esp-idf\export.ps1

# build and flash
idf.py -C C:\tcam\tCam-Mini\firmware build
idf.py -C C:\tcam\tCam-Mini\firmware -p COM10 -b 921600 flash monitor
```


## Common errors

| Symptom | Cause | Fix |
|---|---|---|
| `idf.py : The term 'idf.py' is not recognized` | New shell, environment not activated | Run `C:\esp\esp-idf\export.ps1` or use the Start menu shortcut |
| `ModuleNotFoundError: No module named 'pkg_resources'` | setuptools 81+ removed it | `python -m pip install "setuptools<81"` |
| `Filename too long` during build | IDF or source installed under a long path | Reinstall to `C:\esp`, clone to `C:\tcam` |
| `Detected size(8192k) smaller than ... image header(16384k)`, reboot loop | Built/flashed the tCam handheld project by mistake | `erase-flash`, then rebuild with `-C C:\tcam\tCam-Mini\firmware` |
| A directory literally named `~` appears | PowerShell does not expand `~` in every context | Use full paths; delete the stray directory |
| `idf.py --version` prints `v1.0.3` | That is the launcher executable's version | Harmless; check `git -C C:\esp\esp-idf describe --tags` instead |
| No COM port in Device Manager | Missing CP210x driver, or a charge-only USB cable | Install the Silicon Labs driver; try a known data cable |
