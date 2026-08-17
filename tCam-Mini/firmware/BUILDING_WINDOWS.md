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

v4.4.4 is the last release of the v4 line (2022) and reached end of life in
2024, so **it is no longer offered on https://dl.espressif.com/dl/esp-idf/** —
that page now lists v5.x only.  Beware that `v5.4.4` there is not `v4.4.4`.

The release itself is still available; install it from source.  See
*Installing v4.4.4 from git* below, which is now the normal route.  The rest of
this section applies only if you have an old copy of the offline installer.

When the offline installer runs:

- **Keep the installation directory short** — `C:\Espressif` (the installer's own
  default) or `C:\esp`.  Do not put it under your user profile.  Windows has a
  260-character path limit and the ESP32 build tree is deeply nested; a long
  install path produces `Filename too long` errors part way through the build.
- Let it install its own **Python** and **Git** unless you already have them.
- Tick the checkbox to **register the ESP-IDF PowerShell/CMD shortcuts** in the
  Start menu.  That shortcut is how you open a working build shell.

Installation takes 10–20 minutes.

### Installing v4.4.4 from git

Works whether or not another IDF version is present.  Choose a tools directory
first — `C:\Espressif` if the Espressif installer has already put one there, so
the v4.4 toolchain lands beside the existing ones rather than duplicating a set
under your user profile:

```
[Environment]::SetEnvironmentVariable("IDF_TOOLS_PATH","C:\Espressif","User")
$env:IDF_TOOLS_PATH="C:\Espressif"

cd C:\Espressif\frameworks
git clone -b v4.4.4 --recursive https://github.com/espressif/esp-idf.git esp-idf-v4.4.4
```

Then install the toolchain and build a Python environment for it.  Use a
**Python 3.11 or older** interpreter: 3.12 removed `distutils`, which the v4.4
tooling still imports.  The Espressif installer's bundled interpreter is a good
choice if you have one:

```
C:\Espressif\tools\idf-python\3.11.2\python.exe C:\Espressif\frameworks\esp-idf-v4.4.4\tools\idf_tools.py install
C:\Espressif\tools\idf-python\3.11.2\python.exe C:\Espressif\frameworks\esp-idf-v4.4.4\tools\idf_tools.py install-python-env
```

The first command fetches the GCC 8.4.0 Xtensa toolchain v4.4 builds with; the
second creates an `idf4.4_py3.11_env` virtualenv.  Activate with the framework's
`export.ps1` as in step 4.

There is no Start-menu shortcut for a git install — activate by path.


### If a different IDF version is already installed

v4.4.4 coexists with other versions — each framework lives in its own folder
under `<install dir>\frameworks\` and they share one tools directory.  Put
v4.4.4 in the **same** install directory as the existing version rather than a
new tree, so it reuses the tools directory instead of duplicating gigabytes of
toolchains.  Each version is then selected by running its own `export.ps1`.

This project will **not** build under IDF v5.x — the component layout, the
`esp_https_server` API and the `sdkconfig` format all changed after v4.4.  If a
build fails immediately with unknown Kconfig symbols or missing components,
check `echo $env:IDF_PATH` first; it must end in `esp-idf-v4.4.4`.


## 3. Get the source

Clone the repository somewhere with a short path — the same 260-character limit
applies to the build tree:

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
- open any PowerShell and run the activation script for the v4.4.4 framework:

```
C:\Espressif\frameworks\esp-idf-v4.4.4\export.ps1
```

Substitute your install directory if you did not use `C:\Espressif`.  The
activation banner prints `Setting IDF_PATH:` — confirm it names **v4.4.4** and
not some other version that happens to also be installed.

Verify with `idf.py --version`.  Note: on a Windows install this often prints
something like `ESP-IDF v1.0.3` — that is the version of the small `idf.py`
launcher executable, not the IDF itself, and it is harmless.  To confirm the real
IDF version, run `echo $env:IDF_PATH` — it must end in `esp-idf-v4.4.4`.


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


## 6. Fix the Python `pkg_resources` error

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


## 7. Build

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


## 8. Flash

With the camera plugged in and the COM port from step 1 (`COM10` in this
example):

```
idf.py -C C:\tcam\tCam-Mini\firmware -p COM10 -b 921600 flash monitor
```

`monitor` attaches the serial console at 115200 baud afterwards so you can watch
it boot.  Press **Ctrl+]** to exit the monitor.

If flashing fails to start, hold the board's **Boot** button while the tool says
`Connecting....`, or lower the speed with `-b 115200`.


## 9. If the camera will not boot

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
install ESP-IDF v4.4.4 (git clone + idf_tools.py, see step 2)
[Environment]::SetEnvironmentVariable("SSLKEYLOGFILE",$null,"User")   (if set)
python -m pip install "setuptools<81"     (inside the ESP-IDF 4.4 shell)

# once per PowerShell window
C:\Espressif\frameworks\esp-idf-v4.4.4\export.ps1

# build and flash
idf.py -C C:\tcam\tCam-Mini\firmware build
idf.py -C C:\tcam\tCam-Mini\firmware -p COM10 -b 921600 flash monitor
```


## Common errors

| Symptom | Cause | Fix |
|---|---|---|
| `idf.py : The term 'idf.py' is not recognized` | New shell, environment not activated | Run `C:\Espressif\frameworks\esp-idf-v4.4.4\export.ps1` or use the Start menu shortcut |
| `ModuleNotFoundError: No module named 'pkg_resources'` | setuptools 81+ removed it | `python -m pip install "setuptools<81"` |
| `PermissionError: ... virtual_file.log` inside `urllib3` on every `idf.py` | Security software set `SSLKEYLOGFILE` to an unwritable path | Clear the variable (step 5) |
| `Failed to resolve component 'mdns'` | An IDF v5.x shell is active - `mdns` left the framework after v4.4 | Activate the v4.4.4 framework; check `echo $env:IDF_PATH` |
| Build worked before, now fails oddly after a v5.x attempt | A v5.x configure run rewrote `sdkconfig` with its own Kconfig symbols | `git checkout -- tCam-Mini/firmware/sdkconfig tCam-Mini/firmware/sdkconfig.old` and delete `build\` |
| `Filename too long` during build | IDF or source installed under a long path | Reinstall to `C:\Espressif` or `C:\esp`, clone to `C:\tcam` |
| `Detected size(8192k) smaller than ... image header(16384k)`, reboot loop | Built/flashed the tCam handheld project by mistake | `erase-flash`, then rebuild with `-C C:\tcam\tCam-Mini\firmware` |
| A directory literally named `~` appears | PowerShell does not expand `~` in every context | Use full paths; delete the stray directory |
| `idf.py --version` prints `v1.0.3` | That is the launcher executable's version | Harmless; check `echo $env:IDF_PATH` instead |
| No COM port in Device Manager | Missing CP210x driver, or a charge-only USB cable | Install the Silicon Labs driver; try a known data cable |
