# tCam Finder (hosted page)

**Live: <https://mcgeaverbeaver.github.io/tCam/>** · [▶ demo video](https://youtube.com/shorts/R_bb3d-skMs)

`index.html` here is the **tCam Finder** — a single static page that locates a
tCam-Mini over Bluetooth LE and opens its web interface, wherever the camera
roamed.  A camera's IP address changes between networks; this page's address
never does, so this is the thing to bookmark or add to a home screen.

![tCam Finder](finder.png)

How it works: the 6.15+ firmware advertises a small Bluetooth LE service whose
one characteristic returns the camera's name and the IP address it holds right
now.  The page (Chrome/Edge, any platform with Web Bluetooth) reads it and
navigates to `http://<that address>/`.  All camera traffic stays on the local
network — the page carries no backend, no accounts, and stores the remembered
camera list only in the visitor's own browser.

## Hosting it

Enable GitHub Pages for the repository once (already done for this one):

**Settings → Pages → Source: "Deploy from a branch" → Branch: `main`, folder
`/docs` → Save.**

The finder then serves at `https://<user>.github.io/<repo>/` for anyone.  Web
Bluetooth requires an HTTPS origin, which GitHub Pages provides.

Any other static HTTPS host works identically — the page is self-contained.

## The contract with the firmware

Service UUID `7ca2c9c0-9d3a-4b2e-8e5d-52f1b74a0c1d`, characteristic
`7ca2c9c1-9d3a-4b2e-8e5d-52f1b74a0c1d`, defined in
`tCam-Mini/firmware/main/ble_beacon.c`.  The characteristic read returns json:

```json
{"name":"tCam-Mini-82D5","ip":"192.168.66.11","mode":"sta"}
```

`mode` is `"ap"` when the camera is running its own hotspot (join that WiFi
first; the camera is then always at 192.168.58.1).
