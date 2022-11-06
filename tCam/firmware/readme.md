## tCam Firmware
tCam firmware for gCore is an Espressif IDF project.  You need to have the Espressif v4.4.2 IDF installed to build the firmware.  Pre-compiled binary files are provided (```precompiled``` directory here) and can be programmed using the IDF tools or a Windows utility as described in the ```programming``` directory elsewhere in this repostitory.

### Building
The ```sdkconfig``` file contains ESP32 configuration and build-specific information.  All camera-specific configuration is in the ```main/system_config.h``` file.

To build the project: ```idf.py build```

To load the project onto gCore: 

1. Plug gCore into your computer
2. Turn gCore on
3. ```idf.py -p PORT -b 921600 flash``` where PORT is the name of the serial port associated with gCore.

To monitor diagnostic information from the firmware: ```idf.py -p PORT```.  Output is at 115200 baud.  Note the command will reboot the camera.


### Command Interface
The camera is capable of executing a set of commands and generating responses or sending image and file data when connected to a remote computer.  It can support one remote connection at a time.  Commands and responses are encoded as json-structured strings.  The command interface exists as a TCP/IP socket at port 5001 when using WiFi.

Each json command or response is delimited by two characters.  A Start delimiter (8-bit value 0x02) precedes the json string.  An End delimiter (8-bit value 0x03) follows the json string.  The json string may be tightly packed or may contain white space.

```<0x02><json string><0x03>```

The camera currently supports the following commands.  They are a superset of the commands supported by tCam-Mini.  tCam-specific commands are noted with an asterisk (*).  The communicating application should wait for a response from commands that generate one before issuing subsequent commands (although the camera command buffer is 12,288 bytes (sized for the ```fw_segment``` command) and can support multiple short commands).

| Command | Description |
| --- | --- |
| [get_status](#get_status) | Returns a packet with camera status.  The application uses this to verify communication with the camera. |
| [get_image](#get_image) | Returns a packet with metadata, radiometric (or AGC) image data and Lepton telemetry objects. |
| [set_time](#set_time) | Set the camera's clock. |
| [get_config](#get_config) | Returns a packet with the camera's current settings. |
| [get\_lep_cci](#get_lep_cci) | Reads and returns specified data from the Lepton's CCI interface. |
| [run_ffc](#run_ffc) | Initiates a Lepton Flat Field Correction. |
| [set_config](#set_config) | Set the camera's settings. |
| [set\_lep_cci](#set_lep_cci) | Writes specified data to the Lepton's CCI interface. |
| [set_spotmeter](#set_spotmeter) | Set the spotmeter location in the Lepton. |
| [stream_on](#stream_on) | Starts the camera streaming images and sets the interval between images and an optional number of images to stream. |
| [stream_off](#stream_off) | Stops the camera from streaming images. |
| [get_wifi](#get_wifi) | Returns a packet with the camera's current WiFi and Network configuration. |
| [set_wifi](#set_wifi) | Set the camera's WiFi and Network configuration.  The WiFi subsystem is immediately restarted.  The application should immediately close its socket after sending this command. |
| [take_picture](#take_picture)* | Command the camera to take a picture and store it on the local Micro-SD card. |
| [record_on](#record_on)* | Command the camera to start recording and storing the video on the local Micro-SD card. |
| [record_off](#record_off)* | Command the camera to stop recording a video. |
| [get\_filesystem_list](#get_filesystem_list)* | |
| [get_file](#get_file)* | |
| [delete\_filesystem_obj](#delete_filesystem_obj)* | |
| [poweroff](#poweroff)* | Command the camera to turn off. |
| [fw\_update_request](#fw_update_request) | Informs the camera of a OTA FW update size and revision and starts it blinking the LED alternating between red and green to signal to the user a OTA FW update has been requested. |
| [fw_segment](#fw_segment) | Sends a sequential chunk of the new FW during an OTA FW update. |
| [dump_screen](#dump_screen)* | Get the raw image data from the currently displayed screen.  Support for this command is typically not compiled into the binaries (I only compile it when I want a screen dump of a new screen for documentation). |

The camera generates the following responses.

| Response | Description |
| --- | --- |
| [cam_info](#cam_info-messages) | Generic information packet from the camera.  Status for commands that do not generate any other response.  May also contain alert or error messages from the camera. |
| [cci_reg](#cci_reg-response) | Response to both get\_cci\_reg and set\_cci\_reg commands. |
| [config](#get_config-response) | Response to get_config command. |
| [get_fw](#get_fw) | Request a sequential chunk of the new FW during an OTA FW update. |
| [image](#image-response) | Sent by the camera over the network as a response to a get\_image or get_file command or initiated periodically by the camera if streaming has been enabled. |
| [status](#get_status-response) | Response to get_status command. |
| [wifi](#get_wifi-response) | Response to get_wifi command. |
| [filesystem_list](#filesystem_list-response)* | Response to get\_filesystem_list command. |
| [video_info](#video_info-response)* | Final response 
| [screen\_dump_response](#screen_dump_response-response)* | Response to dump_screen command. |

Commands and responses are detailed below with example json strings.

#### get_status
```{"cmd":"get_status"}```

#### get_status response
```
{
	"status": {
		"Camera":"tCam-Mini-EFB5",
		"Model":262402,
		"Version":"2.0",
		"Time":"17:33:49.0",
		"Date":"2/3/21"
	}
}
```
The get_status response may include additional information for other camera models (for example Battery/Charge information).

| Status Item | Description |
| --- | --- |
| Camera | AP SSID also used as the camera name. |
| Model | A 32-bit mask with camera information (see below). Software can use the camera model to enable and disable camera specific functionality. |
| Version | Firmware version. "Major Revision . Minor Revision" |
| Time | Current Camera Time including milliseconds: HH:MM:SS.MSEC |
| Date | Current Camera Date: MM/DD/YY |

| Model Bit | Description |
| --- | --- |
| 31:19 | Reserved - Read as 0 |
| 18 | Supports over-the-air FW updates |
| 17 | Has Filesystem Flag |
| 16 | Has Battery Flag |
| 15:14 | Reserved - Read as 0 |
| 13:12 | Interface Type (FW 3.0 and beyond) |
| | 0 0 - WiFi Interface |
| | 0 1 - Hardware Interface (Serial/SPI Interface) |
| | 1 0 - Ethernet Interface (tCam-POE only) |
| | 1 1 - Reserved |
| 11:10 | Reserved - Read as 0 |
| 9:8 | Lepton Type (FW 3.1 and beyond) |
| | 0 0 - Lepton 3.5 |
| | 0 1 - Lepton 3.0 |
| | 1 0 - Reserved |
| | 1 1 - Reserved |
| 7:0 | Camera Model Number - tCam-Mini reports 2 |

#### get_image
```{"cmd":"get_image"}```

#### image response
Response to get\_image or get\_file commands, or initiated periodically while streaming.

```
{
	"metadata":	{
		"Camera": "tCam-Mini-EFB5",
		"Model": 2,
		"Version": "1.0",
		"Time": "19:00:58.644",
		"Date": "2/3/21"
	},
	"radiometric": "I3Ypdg12B3YPdgt2BXYRdgF2A3YFdgF2AXYNdv91+3ULdvd..."
	"telemetry": "DgCDMSkAMAgAABBhCIKyzJpkj..."
}
```

| Image Item | Description |
| --- | --- |
| metadata | Camera status information at the time the image was acquired. |
| radiometric | Base64 encoded Lepton pixel data (19,200 16-bit words / 38,400 bytes).  Each pixel contains a 16-bit absolute (Kelvin) temperature value when the Lepton is operating in Radiometric output mode.  The Lepton's gain mode specifies the resolution (0.01 K in High gain, 0.1 K in Low gain). Each pixel contains an 8-bit value when the Lepton has AGC enabled. |
| telemetry | Base64 encoded Lepton telemetry data (240 16-bit words / 480 bytes).  See below for some important telemetry words and the Lepton Datasheet for a full description of the telemetry contents. |

#### get\_lep_cci
```
{
  "cmd": "get_lep_cci",
  "args": {
    "command": 20172,
    "length": 4
  }
}
```

| get\_lep_cci argument | Description |
| --- | --- |
| command | Decimal representation of the 16-bit Lepton COMMAND register value. For example, the value "20172" above is 0x4ECC (RAD Spotmeter Region of Interest). |
| length | Decimal number of 16-bit words to read (1-512). |

#### cci\_reg response
Response for get\_lep_cci.

```
{
  "cci_reg": {
    "command":20172,
    "length":4,
    "status":6,
    "data":"OwBPADwAUAA="
  }
}
```

| cci_reg Item | Description |
| --- | --- |
| command | Decimal representation of the 16-bit Lepton COMMAND register value read. |
| length | Decimal number of 16-bit words read. |
| status | Decimal representation of the 16-bit Lepton STATUS register with the final status of the read. |
| data | Base64 encoded Lepton register data. ```length``` 16-bit words.  For length <= 16 the data is from the Data Register 0 - 15.  For length > 16 the data is from Block Data Buffer 0. |

#### set_time
```
{
  "cmd": "set_time",
  "args": {
    "sec": 14,
    "min": 10,
    "hour": 21,
    "dow": 2,
    "day": 18,
    "mon": 5,
    "year": 50
  }
}
```
All set_time args must be included.

| set_time argument | Description |
| --- | --- |
| sec | Seconds 0-59 |
| min | Minutes 0-59 |
| hour | Hour 0-23 |
| dow | Day of Week starting with Sunday 1-7 |
| day | Day of Month 1-28 to 1-31 depending |
| mon | Month 1-12 |
| year | Year offset from 1970 |

#### get_config
```{"cmd":"get_config"}```

#### get_config response
Response to get_config.

```
{
	"config":{
		"agc_enabled":0,
		"emissivity":100,
		"gain_mode":0
	}
}
```

| Status Item | Description |
| --- | --- |
| agc_enabled | Lepton AGC Mode: 1: Enabled, 0: Disabled (Radiometric output) |
| emissivity | Lepton Emissivity: 1 - 100 (integer percent) |
| gain_mode | Lepton Gain Mode: 0: High, 1: Low, 2: Auto |

#### run_ffc
```{"cmd":"run_ffc"}```

The ```run_ffc``` command generates the following ```cam_info``` response.

```
{
  "cam_info": {
    "info_value": 1,
    "info_string": "run_ffc success"
  }
}
```

#### set_config
```
{
  "cmd": "set_config",
  "args": {
    "agc_enabled": 1,
    "emissivity": 98,
    "gain_mode": 2
  }
}
```
Individual args values may be left out.  The camera will use the existing value.

| set_config argument | Description |
| --- | --- |
| agc_enabled | Lepton AGC Mode: 1: Enabled, 0: Disabled (Radiometric output) |
| emissivity | Lepton Emissivity: 1 - 100 (integer percent) |
| gain_mode | Lepton Gain Mode: 0: High, 1: Low, 2: Auto |

```set_config``` items are stored in non-volatile flash memory for cameras operating with WiFi enabled, allowing them to persist across power cycles.  They are not stored in non-volatile memory for cameras operating with the Hardware Interface enabled and only persist until power is removed.  It is assumed the host device will configure the desired items during an initialization sequence.

#### set\_lep_cci
```
{
  "cmd":"set_lep_cci",
  "args": {
    "command":20173,
    "length":4,
    "data":"OwBPADwAUAA="
  }
}
```

| set\_cci_reg argument | Description |
| --- | --- |
| command | Decimal representation of the 16-bit Lepton COMMAND register value.  Note bit 0 must be set for a write. |
| length | Decimal number of 16-bit words written. |
| data | Base64 encoded Lepton register data to write. ```length``` 16-bit words.  For length <= 16 the data will be loaded into the Data Registers.  For length > 16 the data will be loaded into Block Data Buffer 0. |

Note: It is possible to crash the Lepton or camera using the CCI interface.

The ```set_lep_cci``` command generates the following ```cam_info``` response.

```
{
  "cci_reg": {
    "command":20173,
    "length":4,
    "status":6
  }
}
```

| cci_reg Item | Description |
| --- | --- |
| command | Decimal representation of the 16-bit Lepton COMMAND register value.  Note bit 0 will be set for a write. |
| length | Decimal number of 16-bit words written. |
| status | Decimal representation of the 16-bit Lepton STATUS register with the final status of the write. |

#### set_spotmeter
```
{
  "cmd": "set_spotmeter",
  "args": {
    "c1": 79,
    "c2": 80,
    "r1": 59,
    "r2": 60
  }
}
```

| set_spotmeter argument | Description |
| --- | --- |
| c1 | Spotmeter column 1: Left X-axis spotmeter box coordinate (0-159) |
| c2 | Spotmeter column 2: Right X-axis spotmeter box coordinate (0-159) |
| r1 | Spotmeter row 1: Top Y-axis spotmeter box coordinate (0-119) |
| r2 | Spotmeter row 2: Bottom Y-axis spotmeter box coordinate (0-119) |

Column c1 should be less than or equal to c2.  Row r1 should be less than or equal to r2.  All four argument values must be specified.  They specify the box of pixels the Lepton uses to calculate the spotmeter temperature (which is contained in the image telemetry).

#### stream_on
```
{
	"cmd":"stream_on",
	"args":{
		"delay_msec":0,
		"num_frames":0
	}
}
```

| stream\_on argument | Description |
| --- | --- |
| delay_msec | Delay between images.  Set to 0 for fastest possible rate.  Set to a number greater than 250 to specify the delay between images in mSec. |
| num_frames | Number of frames to send before ending the stream session.  Set to 0 for no limit (set\_stream_off must be sent to end streaming). |

Streaming is a slightly special case for the command interface.  Responses are typically generated after receiving the associated get command.  However the image response is generated repeatedly by the camera after streaming has been enabled at the rate, and for the number of times, specified in the set\_stream\_on command.

#### stream_off
```{"cmd":"stream_off"}```

#### take_picture
```{"cmd":"take_picture"|```

Returns a ```cam_info``` message indicating success or failure if there is no Micro-SD card installed.

#### record_on
```
{
	"cmd":"record_on",
	"args":{
		"delay_msec":0,
		"num_frames":0
	}
}
```

| stream\_on argument | Description |
| --- | --- |
| delay_msec | Delay between images.  Set to 0 for fastest possible rate.  Set to a number greater than 250 to specify the delay between images in mSec. |
| num_frames | Number of frames to send before ending the recording session.  Set to 0 for no limit (record_off must be sent to end streaming). |

Returns a ```cam_info``` message indicating success or failure if there is no Micro-SD card installed.

#### record_off
```{"cmd":"record_off"|```

Returns a ```cam_info``` message indicating success or failure if there is no Micro-SD card installed.

#### get_wifi
```{"cmd":"get_wifi"}```

#### get_wifi response
```
{
  "wifi": {
    "ap_ssid": "tCam-Mini-EFB5",
    "sta_ssid": "HomeNetwork",
    "flags": 143,
    "ap_ip_addr": "192.168.4.1",
    "sta_ip_addr": "10.0.1.144",
    "sta_netmask":"255.255.255.0",
    "cur_ip_addr": "10.0.1.144"
  }
}
```

| Response | Description |
| --- | --- |
| ap_ssid | The camera's current AP-mode SSID and also the camera name as reported in the metadata and status packets. |
| sta_ssid | The SSID used when the camera's WiFi is configured as a client (STAtion mode) |
| flags | 8-bit Wifi Status as a decimal number (see below). |
| ap\_ip_addr | The camera's IP address when it is in AP mode. |
| sta\_ip_addr | The static IP address to use when the camera is a client and configured to use a static IP. |
| sta_netmask | The netmask to use when the camera is a client and configured to use a static IP. |
| cur\_ip_addr | The camera's current IP address.  This may be a DHCP served address if the camera is configured in Client mode with static IP addresses disabled. |

Password information is not sent as part of the wifi response.

| Flag Bit | Description |
| --- | --- |
| 7 | Client Mode - Set to 1 for Client Mode, 0 for AP mode. |
| 6:5 | Unused, will be set to 0. |
| 4 | Static IP - Set to 1 to use a Static IP, 0 to request an IP via DHCP. |
| 3 | Bit 3: Wifi Connected - Set to 1 when connected to another device. |
| 2 | Wifi Client Running - Set to 1 when the client has been started, 0 when disabled (obviously this bit will never be 0). |
| 1 | Wifi Initialized - Set to 1 when the WiFi subsystem has been successfully initialized (obviously this bit will never be 0). |
| 0 | Bit 0: Wifi Enabled - Set to 1 when Wifi has been enabled, 0 when Wifi has been disabled. |

#### set_wifi
```
{
  "cmd": "set_wifi",
  "args": {
    "ap_ssid": "ANewApName",
    "ap_pw: "apassword",
    "ap_ip_addr": "192.168.4.1",
    "flags": 145,
    "sta_ssid": "HomeNetwork",
    "sta_pw": "anotherpassword",
    "sta_ip_addr": "10.0.1.144",
    "sta_netmask": "255.255.255.0"
  }
}
```

Individual args values may be left out (for example to just set AP or Client (STA) values.  The camera will use the existing value.

| set_wifi argument | Description |
| --- | --- |
| ap_ssid | Set the AP-mode SSID and also the camera name as reported in the metadata and status objects (1-32 characters). |
| ap_pw | Set the AP-mode password (8-63 characters). |
| ap\_ip_addr | The camera's IP address when it is in AP mode. |
| flags | Set WiFi configuration (see below). |
| sta_ssid | Set the client-mode (STA) SSID (1-32 characters). |
| sta_pw | Set the client-mode (STA) password (8-63 characters). |
| sta\_ip_addr | Set the static IP address to use when the camera as a client and configured to use a static IP. |
| sta_netmask | Set the netmask to use when the camera as a client and configured to use a static IP. |

Only a subset of the flags argument are used to configure WiFi operation.  Other bit positions are ignored.

| Flag Bit | Description |
| --- | --- |
| 7 | Client Mode - Set to 1 for Client mode, 0 for AP mode. |
| 4 | Static IP - Set to 1 to use a Static IP, 0 to request an IP via DHCP when operating in Client mode. |
| 0 | Bit 0: Wifi Enabled - Set to 1 to enable Wifi, 0 to disable Wifi. |

The command will fail if an SSID is zero length or greater than 32 characters or if a password is less than 8 characters or greater than 63 characters.

Although I do not recommend it, use the following procedure to set a zero-length (null) password.

1. Reset the Wifi configuration using the Wifi reset button.  This sets all password fields to a null value.
2. Use the Desktop application to configure the Wifi but make sure the password field is not updated (de-select the Update checkbox).  Alternatively send a set_wifi command without a password (```ap_pw``` or ```sta_pw``` argument).

#### get\_filesystem_list
Command to get the directories at the root of the filesystem:

```
{
	"cmd": "get_filesystem_list",
	"args": {
		"dir_name":"/"
	}
}
```
Command to get the files in a specific directory.

```
{
	"cmd": "get_filesystem_list",
	"args": {
		"dir_name": "tcam_22_11_05"
	}
}
```

The ```get_filesystem_list``` command is used to return a list of the top level directories or the files within one of those directories in the ```filesystem_list``` response.  Typically the individual entries in the list of top level directories are used in subsequent commands to get a list of all files in the filesystem.

#### filesystem_list response
Response containing list of directories at the root of the filesystem.

```
{
	"filesystem_list": {
		"dir_name":"/",
		"name_list":"tcam_00_01_01,tcam_21_04_02,tcam_22_10_30,tcam_22_10_31,tcam_22_11_02,tcam_22_11_05,"
	}
}
```

Response containing a list of files within a specific directory.

```
{
	"filesystem_list": {
		"dir_name": "tcam_22_11_05",
		"name_list": "img_13_33_40.tjsn,mov_13_33_47.tmjsn,"
	}
}
```

| Response | Description |
| --- | --- |
| dir_name | The name of the directory being listed. |
| name_list | A comma separated list of items in that directory.  The final item is followed by a comma. |

#### get_file

```
{
	"cmd": "get_file",
	"args" {
		"dir_name": "tcam_22_11_05",
		"file_name": "mov_13_33_47.tmjsn"
	}
}
```

| get\_file argument | Description |
| --- | --- |
| dir_name | Directory name containing file. |
| file_name | File in the specified directory to get. |

The ```get_file``` command returns a ```cam_info``` response containing failure information if the file cannot be found.  It returns one or more ```image``` responses and optionally a ```video_info``` response if the file is present.

An image file will return a single ```image``` response with the contents of the image file.

A video file will return a series of ```image``` responses, one for each image in the video, followed by a ```video_info``` response (which is the final component in a tmjsn video file).

Please see the description of the tmjsn file format in the Desktop Application directory readme.  To reconstruct a tmjsn file from the responses to a ```get_file``` command append the END\_OF_JSON delimiter (0x03) after each ```image``` json string.

#### video_info response

```
{
	"video_info": {
		"end_date":"11/5/22",
		"end_time":"13:33:50.438",
		"num_frames":25,
		"start_date":"11/5/22",
		"start_time":"13:33:47.638",
		"version":1
	}
}
```

The "video_info" json text string contains the starting and ending timestamps and number of frames. It is used by applications to validate the file and also determine if it should show the "Fast Forward" control for videos with long delays between frames.

#### delete\_filsystem_obj

```
{
	"cmd": "delete_filesystem_obj",
	"args": {
		"dir_name":"tcam_22_11_05",
		"file_name":"mov_13_33_47.tmjsn"
	}
}
```

| delete\_filsystem_obj argument | Description |
| --- | --- |
| dir_name | Directory name containing file. |
| file_name | File in the specified directory to delete. |

Returns a ```cam_info``` message indicating success or failure if there is no file.

#### poweroff
```{"cmd": "poweroff"}```

There is no response as the camera powers off immediately after receiving the command.

#### cam_info messages
The ```cam_info``` response is generated for commands that do not return a response such as ```set_clock```, ```set_config``` or ```set_wifi``` with status about the success or failure of the command.  It can also be generated by the camera for certain errors.

For example, ```cam_info``` for ```set_clock``` looks like.

```
{
  "cam_info": {
    "info_value": 1,
    "info_string": "set_clock success"
  }
}
```

| cam_info Item | Description |
| --- | --- |
| info_value | A decimal status code (see below). |
| info_string | An information string. |

| cam_info status code | Description |
| --- | --- |
| 0 | Command NACK - the command failed.  See the information string for more information. |
| 1 | Command ACK - the command succeeded. |
| 2 | Command unimplemented - the camera firmware does not implement the command. |
| 3 | Command bad - the command was incorrectly formatted or was not a json string. |
| 4 | Internal Error - the camera detected an internal error.  See the information string for more information. |
| 5 | Debug Message - The information string contains an internal debug message from the camera (not normally generated). |

#### fw\_update_request
The ```fw_update_request``` command initiates the FW update process.

```
{
  "cmd":"fw_update_request",
  "args":{"length":730976,"version":"2.0"}
}
```

All arguments are required.

| fw\_update_request argument | Description |
| --- | --- |
| length | Length of binary file in bytes. |
| version | Binary file version.  This must match the build version embedded in the binary file. |

#### get_fw
The camera requests a chunk of the binary file using the ```get_fw``` response after the user has initiated the update.  Currently the camera will request a maximum of 8192 bytes.

```
{
  "get_fw":{
    "start":0,
    "length":8192
  }
}
```

| get_fw argument Item | Description |
| --- | --- |
| start | Starting byte of the current chunk (offset into binary file). |
| length | Number of bytes to send in a subsequent ```fw_segment```. |

#### fw_segment
Generated in response to a ```get_fw``` request.  Must always use the arguments in the ```get_fw``` request.

```
{
  "cmd":"fw_segment",
  "args":{
    "start":0,
    "length":8192,
    "data":"6QYCMPwUCEDuAAAAAAADAAAAAAAAAAABIABAPwjEAQAyVM2rAAAAAAAAA..."
  }
}
```

| fw_segment Argument | Description |
| --- | --- |
| start | Starting byte of current chunk. |
| length | Length of chunk in bytes. |
| data | Base64 encoded binary data chunk. |

#### dump_screen

```{"cmd": "dump_screen"}```

A ```cam_info``` message indicating unsupported command will be sent in response if support for this command has not been compiled into the binary.  Otherwise a series of ```screen_dump_response``` responses will be sent.

#### screen\_dump_response response

```
{
	"screen_dump_response": {
		"start": 0,
		"length": 16384,
		"data": "I3Ypdg12B3YPdgt2BXYRdgF2A3YFdgF2AXYNdv91+3ULdvd..."
	}
}
```

| Response | Description |
| --- | --- |
| start | Starting byte location for this segment of the image. |
| length | Number of bytes in this segment. |
| data | Base64 encoded segment data.  Data is 16-bit RGB565 pixel data encoded in Big Endian format for the tCam 480x320 pixel display. |

Currently the firmware sends up to 16,384 bytes at a time for the 307,200 byte display frame buffer (480 x 320 x 2 bytes/pixel) requiring 19 ```screen_dump_response``` packets (the final packet contains only 12,288 bytes).  The frame buffer is organized with the upper-left corner the pixel position (0, 0) and first pixel sent, row by row.

### OTA FW Update Process
The OTA FW update process consists of several steps.  The FW is contained in the binary file ```tCamMini.bin``` in the precompiled FW directory or built using the Espressif IDF.  No other binary files are required.

1. An external computer initiates the update process by sending a ```fw_update_request```.
2. The camera starts blinking the LED in an alternating red/green pattern to indicate a FW update has been requested.  The user must press the Wifi Reset Button to confirm the update should proceed.
3. The camera sends a ```get_fw``` to request a chunk of data from the computer.
4. The computer sends a ```fw_segment``` with the requested data.
5. Steps 3 and 4 are repeated until the entire firmware binary file has been transferred.
6. The camera validates the binary file and sends a ```cam\_info``` indicating if the update is successful or has failed.  If successful the camera then reboots into the new firmware.

### Important Telemetry Locations

| Offset | 16-bit Word Description |
| --- | --- |
| 3 | Status Low (see below) |
| 4 | Status High |
| 99 | Emissivity (scaled by 8192) |
| 208 | TLinear Enabled Flag - 0 = disabled, 1 = enabled |
| 209 | TLinear Resolution Flag - 0 = 0.1, 1 = 0.01 |
| 210 | Spot Meter Mean - 16-bit radiometric value |
| 214 | Spot Meter Y1 coordinate |
| 215 | Spot Meter X1 coordinate |
| 216 | Spot Meter Y2 coordinate |
| 217 | Spot Meter X2 coordinate |


| Bit | Status Bit Description |
| --- | --- |
| 20 | Over Temperature Shut Down imminent (shutdown in 10 seconds) |
| 15 | Shutter Lockout - 0 = not locked out, 1 = locked out (outside of valid operating temperature range -10°C to 80°C) |
| 12 | AGC State - 0 = disabled, 1 = enabled |
| 5:4 | FFC State - 0 = Not commanded, 1 = Imminent, 2 = Running, 3 = Complete |
| 3 | FFC Desired |

#### Using telemetry data

The Status AGC State bit indicates the form of the pixel data.  When AGC is enabled the lepton performs a histogram analysis of the data and the pixel data is 8-bits (low byte of each 16-bit word) designed to directly index a palette array for display.  When AGC is disabled the lepton outputs 16-bit radiometric data in each pixel.  This data represents the actual temperature of each pixel in degrees Kelvin scaled by either 10 (TLinear Resolution Flag = 0) or 100 (TLinear Resolution Flag = 1).  The TLinear Resolution Flag indicates the gain mode of the Lepton (0 = Low Gain, 1 = High Gain).

The radiometric pixel temperature in °C may be computed using the following formula where TLinear resolution is 0.1 if TLinear Resolution Flag is 0 and 0.01 if TLinear Resolution Flag is 1.

``` TempC = Pixel[15:0] * TLinear_resolution - 273.15```

The Spot Meter average temperature value is computed using the same formula, substituting the Spot Meter Mean for pixel data, and is valid independent of AGC enabled or disabled.  The Spot Meter is the average value of the pixels in the box defined by the Spot Meter (X1, Y1) and (X2, Y2) coordinates.

The FFC State may be used to identify when the lepton is preparing to perform a FFC (flat field correction) and is performing a FFC.  While performing a FFC the lepton closes the shutter in front of the sensor array.  Image data is not available while a FFC is being performed.
