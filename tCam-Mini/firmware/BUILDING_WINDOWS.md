# Building tCam-Mini firmware on Windows

Step-by-step setup for a fresh Windows 10/11 machine.  The firmware targets
**ESP-IDF v5.5**.

Target hardware: ESP32-WROVER-E, 8 MB flash, CP2102N USB-serial bridge.


## 1. Install the USB-serial driver

The board's USB bridge is a Silicon Labs CP2102N.  Windows 11 usually installs a
driver automatically, but the vendor driver is more reliable.

Download and install the **CP210x Universal Windows Driver** from Silicon Labs:
https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers

Plug the camera in, open Device Manager, and look under **Ports (COM & LPT)**.
You should see `Silicon Labs CP210x USB to UART Bridge (COMn)`.  Note the COM
number — you need it to flash.


## 2. Install ESP-IDF v5.5

Download the **ESP-IDF v5.5 Offline Installer** from
https://dl.espressif.com/dl/esp-idf/ and run it.

- **Keep the installation directory short** — `C:\Espressif` (the installer's own
  default) is fine.  Do not put it under your user profile.  Windows has a
  260-character path limit and the ESP32 build tree is deeply nested; a long
  install path produces `Filename too long` errors part way through the build.
- Let it install its own **Python** and **Git** unless you already have them.
- Tick the checkbox to **register the ESP-IDF PowerShell/CMD shortcuts** in the
  Start menu.  That shortcut is how you open a working build shell.

Installation takes 10–20 minutes.

Other IDF versions can coexist: each lives in its own folder under
`<install dir>\frameworks\` and they share one tools directory.  Point a second
installer at the same install directory rather than a new tree so it reuses the
toolchains instead of duplicating gigabytes of them.  Each version is selected
by running its own `export.ps1`.


## 3. Get the source

Clone the repository somewhere with a short path — the same 260-character limit
applies to the build tree:

```
cd C:\
git clone https://github.com/McGeaverBeaver/tCam.git tcam
```

That gives you `C:\tcam`.  No further dependency fetching is needed: the one
component that does not ship with the IDF (`mdns`) is vendored in the
repository, so the build works without reaching the component registry.


## 4. Open a build shell — every time

**The ESP-IDF environment is per-shell and does not persist.**  Closing the
PowerShell window discards it; a fresh window knows nothing about `idf.py`.  This
is the single most common reason for `idf.py : The term 'idf.py' is not
recognized`.

Two ways to get a working shell:

- Start menu → **ESP-IDF 5.5 PowerShell** (or *ESP-IDF 5.5 CMD*), or
- open any PowerShell and run the activation script:

```
C:\Espressif\frameworks\esp-idf-v5.5\export.ps1
```

Substitute your install directory and version folder if they differ.  The
activation banner prints `Setting IDF_PATH:` — confirm it names the version you
intend, and check with `echo $env:IDF_PATH` if a build behaves strangely.

Note that `idf.py --version` on Windows often prints something like
`ESP-IDF v1.0.3` — that is the version of the small `idf.py` launcher
executable, not the IDF itself, and it is harmless.


## 5. Clear `SSLKEYLOGFILE` if it is set

Some security software (endpoint protection with file virtualisation, corporate
DLP agents) sets a machine-wide `SSLKEYLOGFILE` environment variable pointing at
a path the user cannot write, typically a volume-GUID path such as
`\\?\Volume{...}\virtual_file.log`.  Python's `urllib3` opens that file whenever
`requests` is imported, so **every** `idf.py` invocation dies before it does any
work:

```
  File "...\urllib3\util\ssl_.py", line 359, in create_urllib3_context
    context.keylog_filename = sslkeylogfile
PermissionError: [Errno 13] Permission denied: '\\?\Volume{...}\virtual_file.log'
```

The variable only exists to dump TLS session keys for packet-capture debugging;
nothing needs it.  Find where it is set:

```
$env:SSLKEYLOGFILE
[Environment]::GetEnvironmentVariable("SSLKEYLOGFILE","User")
[Environment]::GetEnvironmentVariable("SSLKEYLOGFILE","Machine")
```

Clear it for your account and for the current window:

```
[Environment]::SetEnvironmentVariable("SSLKEYLOGFILE",$null,"User")
$env:SSLKEYLOGFILE=""
```

If the *Machine* scope held the value, clear that from an **Administrator**
PowerShell:

```
[Environment]::SetEnvironmentVariable("SSLKEYLOGFILE",$null,"Machine")
```

Close all PowerShell windows and open a fresh one afterwards.


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
with the binary size and how much of the app partition is free.


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

Erasing also clears saved WiFi credentials, so the camera comes back up
broadcasting its own access point.


## Configuration

The web UI's compressed archive and its app icons are generated into the build
directory every time you build, from `main/www/index.html`, which is the only
one of them under version control.  Nothing generated is committed: zlib output
differs slightly between machines, so a committed archive meant every build left
the working tree dirty and the next `git pull` refused to merge.  Edit
`index.html` and rebuild; there is no separate step.

`sdkconfig` is generated and can be deleted at any time; it is rebuilt from
`sdkconfig.defaults`, which holds every deliberate deviation from the IDF
defaults with the reason recorded beside it.  Change settings there, not in the
generated file, and delete `sdkconfig` to pick the change up.  All
camera-specific configuration (pin assignments, task priorities, timeouts) is in
`main/system_config.h`.


## Quick reference

```
# once per machine
install CP210x driver
install ESP-IDF v5.5 offline installer to C:\Espressif
[Environment]::SetEnvironmentVariable("SSLKEYLOGFILE",$null,"User")   (if set)

# once per PowerShell window
C:\Espressif\frameworks\esp-idf-v5.5\export.ps1

# build and flash
idf.py -C C:\tcam\tCam-Mini\firmware build
idf.py -C C:\tcam\tCam-Mini\firmware -p COM10 -b 921600 flash monitor
```


## Common errors

| Symptom | Cause | Fix |
|---|---|---|
| `idf.py : The term 'idf.py' is not recognized` | New shell, environment not activated | Run `export.ps1` or use the Start menu shortcut |
| `PermissionError: ... virtual_file.log` inside `urllib3` on every `idf.py` | Security software set `SSLKEYLOGFILE` to an unwritable path | Clear the variable (step 5) |
| `Failed to resolve component 'mdns'` | An IDF v4.x shell is active | Activate v5.5; check `echo $env:IDF_PATH` |
| Kconfig or component errors right after switching IDF versions | A `sdkconfig` left over from the other version | Delete `sdkconfig` and the `build` directory, then rebuild |
| `Filename too long` during build | IDF or source installed under a long path | Reinstall to `C:\Espressif`, clone to `C:\tcam` |
| `Detected size(8192k) smaller than ... image header(16384k)`, reboot loop | Built/flashed the tCam handheld project by mistake | `erase-flash`, then rebuild with `-C C:\tcam\tCam-Mini\firmware` |
| A directory literally named `~` appears | PowerShell does not expand `~` in every context | Use full paths; delete the stray directory |
| `idf.py --version` prints `v1.0.3` | That is the launcher executable's version | Harmless; check `echo $env:IDF_PATH` instead |
| No COM port in Device Manager | Missing CP210x driver, or a charge-only USB cable | Install the Silicon Labs driver; try a known data cable |
