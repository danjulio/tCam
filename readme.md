## ESP32-based Thermal Imaging Cameras
tCam, tCam-Mini, tCam-POE and tCam-Eth are four cameras I designed around the ESP32 and Lepton 3.5.  They are designed to provide easy access to radiometric data from the Lepton.  Radiometric data is useful because it contains temperature information for each pixel in the camera's image allowing for all kinds of data analysis even if the image is stored to a file.

This repository was created on Nov 6, 2022 from the ESP32 section of my original [Lepton](https://github.com/danjulio/lepton) repository to reduce the size of the portion of that directory that most people are interested in.

### tCam
tCam is a full featured, battery powered camera with a local touchscreen display, local storage and a WiFi interface.  It is comprised of a [gCore]() and tCam-Mini.  A tCam kit can be purchased from Group Gets [here](https://store.groupgets.com/products/tcam-kit).

![tCam](tCam/pictures/tcam_iron.png)

### tCam-Mini
tCam-Mini is a smaller camera designed for streaming and remote access.  It supports a Wifi or hardwired interface.  It can be built using development boards or a tested unit can be purchased from Group Gets [with built in antenna](https://store.groupgets.com/products/tcam-mini-rev4-wireless-streaming-thermal-camera-board) or [with an external antenna](https://store.groupgets.com/products/tcam-mini-rev4-external-antenna-wireless-streaming-thermal-camera-board).

![tCam-Mini](pictures/tcam_mini.png)
(Photo Credit: Matthew Navarro)

