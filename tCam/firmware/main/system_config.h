/*
 * System Configuration File
 *
 * Contains system definition and configurable items.
 *
 * Copyright 2020-2022 Dan Julio
 *
 * This file is part of tCam.
 *
 * tCam is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * tCam is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with tCam.  If not, see <https://www.gnu.org/licenses/>.
 *
 */
#ifndef SYSTEM_CONFIG_H
#define SYSTEM_CONFIG_H

#include "esp_system.h"


// ======================================================================================
// System debug
//

// Undefine to include the system monitoring task (included only for debugging/tuning)
//#define INCLUDE_SYS_MON



// ======================================================================================
// System hardware definitions
//

//
// IO Pins
//   LCD uses VSPI (no MISO)
//   Lepton uses HSPI (no MOSI)
//

#define I2C_MASTER_SDA_IO 21
#define I2C_MASTER_SCL_IO 22

#define LCD_SCK_IO        18
#define LCD_CSN_IO        5
#define LCD_DC_IO         27
#define LCD_MOSI_IO       23

#define LEP_TX_IO         0
#define LEP_RX_IO         19
#define LEP_SCK_IO        32
#define LEP_CSN_IO        26
#define LEP_MISO_IO       25


//
// Hardware Configuration
//
// I2C
#define I2C_MASTER_NUM     1
#define I2C_MASTER_FREQ_HZ 100000

// SPI
//   Lepton uses HSPI (no MOSI)
//   LCD and TS use VSPI
#define LCD_SPI_HOST    VSPI_HOST
#define LEP_SPI_HOST    HSPI_HOST
#define LCD_DMA_NUM     1
#define LEP_DMA_NUM     2
#define LCD_SPI_FREQ_HZ 80000000
#define LEP_SPI_FREQ_HZ  7000000
#define LCD_SPI_MODE    0
#define LEP_SPI_MODE    0



// ======================================================================================
// System configuration
//

// Camera model number - tCam is model 1
#define CAMERA_MODEL_NUM 1

// tCam capabilities mask 
//   Bit  7: 0: Camera model number
//   Bit  9: 8: Lepton Type (set during run time)
//        0  0 - Lepton 3.5
//        0  1 - Lepton 3.0
//        1  0 - Lepton 3.1
//        1  1 - Reserved
//   Bit 13:12: Interface Type
//        0  0 - WiFi (set)
//        0  1 - Serial/SPI interface
//        1  0 - Ethernet
//        1  1 - Reserved
//   Bit     16: Has Battery (set)
//   Bit     17: Has Filesystem (set)
//   Bit     18: Has OTA Firmware Updates (set)
#define CAMERA_CAP_MASK_CORE    0x00070000
#define CAMERA_CAP_MASK_LEP3_5  0x00000000
#define CAMERA_CAP_MASK_LEP3_0  0x00000100
#define CAMERA_CAP_MASK_LEP3_1  0x00000200
#define CAMERA_CAP_MASK_LEP_UNK 0x00000300
#define CAMERA_CAP_MASK_LEP_MSK 0x00000300


// Maximum number of images in a video
//   Each image requires slightly less than JSON_MAX_IMAGE_TEXT_LEN bytes
#define MAX_VIDEO_IMAGES    8192


// Uncomment to include the screen-dump code
//#define SYS_SCREENDUMP_ENABLE


// Little VGL buffer update size
#define LVGL_DISP_BUF_SIZE (480 * 10)

// Theme hue (0-360)
#define GUI_THEME_HUE       240


// Lepton displayed image size
#define LEP_IMG_WIDTH  320
#define LEP_IMG_HEIGHT 240
#define LEP_IMG_PIXELS (LEP_IMG_WIDTH * LEP_IMG_HEIGHT)


// Image (Lepton + Telemetry + Metadata) json object text size
// Based on the following items:
//   1. Base64 encoded Lepton image size: (160x120x2)*4 / 3 = 51200
//   2. Base64 encoded Lepton telemetry size: (80x3x2)*4 / 3 = 640
//   3. Metadata text size: 1024
//   4. Json object overhead (child names, formatting characters, NLs): 128
// Manually calculate this and round to 4-byte boundary
#define JSON_MAX_IMAGE_TEXT_LEN (1024 * 53)

// Maximum firmware update chunk request size
#define FW_UPD_CHUNK_MAX_LEN    (1024 * 8)

// Max command response json object text size
#define JSON_MAX_RSP_TEXT_LEN   2048

// Max Lepton command json object text size
#define JSON_MAX_LEP_CMD_TEXT_LEN 2048

// Command Response Buffer Size (large enough for several responses)
#define CMD_RESPONSE_BUFFER_LEN (JSON_MAX_RSP_TEXT_LEN * 4)

// Lepton Command Buffer Size (large enough for several commands)
#define LEP_COMMAND_BUFFER_LEN (JSON_MAX_LEP_CMD_TEXT_LEN * 4)

// Maximum incoming command json string length
// Large enough for longest command: fw_segment
//    1. Base64 encoded firmware chunk: FW_UPD_CHUNK_MAX_LEN * 4 / 3
//    2. Json object overhead: 128
// Manually calculate this and round to a 4-byte boundary
#define JSON_MAX_CMD_TEXT_LEN   (12 * 1024)

// TCP/IP listening port
#define CMD_PORT 5001

// Serial port baud rate
#define CMD_BAUD_RATE 230400

// Maximum serial port receive buffer (sized to hold a command)
#define SIF_RX_BUFFER_SIZE JSON_MAX_LEP_CMD_TEXT_LEN

// Maximum serial port transmit buffer (sized to hold a non-image response)
#define SIF_TX_BUFFER_SIZE JSON_MAX_RSP_TEXT_LEN

// Filesystem Information Structure buffer (catalog)
//   Holds records for directories (~36 bytes/each) and files in those directories
//   (~32 bytes/each).  This buffer should be sized larger than the most files and
//   directories ever expected to be seen by the system.
#define FILE_INFO_BUFFER_LEN (1024 * 512) 

// Maximum number of names stored in a comma separated catalog listing
#define FILE_MAX_CATALOG_NAMES 150

// Fixed Speed Video playback interval - also used to determine when a video should
// be played back at a fixed rate on the GUI.
#define VIDEO_FIXED_PLAYBACK_MSEC 1000

#endif // SYSTEM_CONFIG_H
