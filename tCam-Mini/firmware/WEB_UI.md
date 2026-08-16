# tCam-Mini on-camera web interface

The camera serves its own user interface. Point a browser at it and you get a live
viewer, camera configuration, network setup and firmware updates — with nothing
installed on the client.

The desktop application, the Android application and the Python library all keep
working exactly as before. The web UI is an additional client, not a replacement,
and it speaks the same json command protocol over a WebSocket instead of a raw TCP
socket.

## Getting to it

**Straight out of the box (access point mode)**

1. Join the camera's WiFi network, `tCam-Mini-XXXX`.
2. Your phone or laptop should pop the interface up by itself. If it does not,
   browse to `http://192.168.58.1/`.

   (The camera's own network is 192.168.58.x specifically because the more common
   192.168.4.x collides with the LAN subnet some consumer routers hand out — a
   collision that makes the camera unreachable from any device that can also see
   that LAN.)

The camera answers every DNS query while it is an access point, so the operating
system's own connectivity check lands on the camera and triggers the usual
"sign in to network" prompt. That prompt *is* the app.

**On your own network (station mode)**

1. Open the web UI in access point mode, go to **Network**.
2. Press **Scan for networks**, pick yours, enter the password.
3. Tick **Join this network at startup** and save.

The camera then appears at `http://<camera-name>.local` — it advertises `_http._tcp`
over mDNS, so it also shows up in network browsers.

**Recovery access point.**  If the configured network is unreachable for about a
minute — a mistyped password, or the camera moved to a new location — it raises its
own access point *while continuing to retry the configured network underneath*.
Join `tCam-Mini-XXXX`, the captive portal opens, and you can point the camera at a
new network (or set it back to access point mode) exactly like first-time setup.
Nothing is stored until you save: if the original network comes back — a rebooted
router — the camera rejoins it and the recovery AP dissolves on its own.  Moving
between locations therefore never needs a reset or a cable.

## What the interface does

The thermal stream owns the whole viewport; controls live in floating glass
surfaces and a slide-over settings panel.

| Tab | Contents |
| --- | --- |
| Display | Palette, output resolution (160/320/640, bilinear), range auto/manual/lock, cursor mode, markers, units |
| Camera | Frame interval, gain mode, emissivity, AGC, FFC, live telemetry (FPA/housing temps, effective gain), PNG and raw capture |
| Network | Camera name, network scan and join, startup mode |
| System | Model and firmware information, firmware update, clock |

The toolbar over the stream has overlay toggle, record, pause/resume, snapshot,
FFC and fullscreen.  In fullscreen the chrome fades out after a moment and returns
on any pointer movement, so the stream gets the entire display.

**Recording and screenshots.**  Record captures video (mp4 or webm, whichever the
browser encodes) at the selected output resolution, frame-accurate to the stream.
Screenshots and recordings share one compositor, and both honor the *capture
overlay* setting: burned-in camera name, timestamp, spot/max/min readouts,
temperature scale and markers — or a completely clean image.  The overlay can be
toggled at any time, including mid-recording (layers button, or the O key).

**Isotherm alarm.**  Pixels at or above a chosen temperature render in a fixed
alarm color regardless of palette.

**Keyboard**: Space pause/resume · R record · S screenshot · F fullscreen ·
O overlay.

**HTTPS.**  The camera also serves the interface on port 443 with a certificate it
generates for itself on first boot (EC P-256, unique per camera, stored in NVS,
valid to 2050).  The browser warns once per device because it is self-signed —
expected for a device with no public name.  HTTP remains primary: captive-portal
probes are plain HTTP, and phones' portal mini-browsers cannot accept certificate
warnings, so there is deliberately no forced redirect.

Tap the image to drop a probe point (tap again to clear), or switch the cursor to
**Move spot** to reposition the camera's own spot meter — the reading measured by
the sensor itself.

The sensor is 160×120; the higher resolution settings interpolate the thermal data
bilinearly in the browser before coloring, the same way the desktop application
renders. **Save PNG** writes at the selected resolution. **Save raw** writes the
camera's own json frame with the `.tjsn` extension — the same format the desktop
application writes, so existing analysis tools still read it.

## How it is put together

The camera sends 16-bit radiometric frames exactly as it always has. All palette
mapping, scaling and measurement happens in the browser. Adding a palette or a
readout therefore costs the camera nothing, and the firmware never grew an image
renderer.

```
browser  ──HTTP──▶  esp_http_server  ──▶  index.html.gz  (embedded in the app image)
         ──WS────▶  web_cmd  ──▶  cmd_utilities  (the existing json command parser)
                                        │
         ◀──WS────  client_if  ◀──  rsp_task  ──▶  legacy TCP socket, port 5001
```

`client_if` arbitrates: one command client at a time, first to connect wins. The
receive and response buffers in `cmd_utilities` are shared singletons, so two
concurrent clients would interleave into each other's packets. A second client is
refused rather than allowed to corrupt the first. The camera's access point now
accepts four associations rather than one, so being refused a *command session* no
longer means being unable to associate at all.

The UI is a single gzipped HTML file (about 11 KB) linked into the application
partition by `EMBED_FILES`. Keeping it there rather than on a separate filesystem
means an OTA update replaces the firmware and its interface atomically — they can
never be left at mismatched versions.

## Firmware updates

**System ▸ Choose firmware .bin** uploads `tCamMini.bin` directly to the camera.

The camera writes to the spare application partition and only switches the boot
target once the whole image has arrived and passed validation, so an interrupted or
corrupt upload leaves the running firmware untouched. Image streaming is suppressed
during an upload so that 53 KB frames are not competing with it.

This replaces the previous update path, in which the camera asked the host for each
chunk over the json protocol and gave up after a fixed number of retries. That
inverted the normal direction of control, required a bespoke implementation in every
client, and failed in ways the operator could not act on. The old protocol still
works for existing clients; nothing was removed.

## Building

`index.html` is the source of truth. `main/CMakeLists.txt` runs `compress_web.py` at
configure time to regenerate `www/index.html.gz`, so the two cannot drift. A
pre-built `.gz` is committed as well, so the build still works if Python is
unavailable.

```
idf.py build
idf.py -p PORT -b 921600 flash
```

Requires ESP-IDF v4.4.4, as before. `CONFIG_HTTPD_WS_SUPPORT` must be enabled — it is
set in the committed `sdkconfig`, but a `sdkconfig` regenerated from defaults will
silently drop the WebSocket endpoint without it.

## Notes and limits

- A frame is roughly 53 KB of base64 json. On a marginal link, raising the frame
  interval on the Camera tab is by far the most effective stabiliser.
- AGC produces a higher contrast picture but the pixels stop being temperatures, so
  the readouts are disabled while it is on.
- The DNS responder only runs in access point mode. On a network with a real
  resolver, hijacking every query would be hostile.
- The interface is served over plain HTTP. It is intended for a camera on a local or
  point-to-point network, and there is no authentication — the same trust model the
  raw command port has always had.
