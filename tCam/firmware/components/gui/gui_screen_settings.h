/*
 * Settings GUI screen related functions, callbacks and event handlers
 *
 * Copyright 2020 Dan Julio
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
#ifndef GUI_SCREEN_SETTINGS_H
#define GUI_SCREEN_SETTINGS_H

#include <stdint.h>
#include <stdbool.h>
#include "lvgl/lvgl.h"


//
// Settings Screen Constants
//

// Screen Title Label
#define S_TITLE_LBL_X    240
#define S_TITLE_LBL_Y    5

// Exit button
#define S_EXIT_BTN_X     430
#define S_EXIT_BTN_Y     10
#define S_EXIT_BTN_W     40
#define S_EXIT_BTN_H     35

// Top of controls
#define SETTING_TOP      40

// Left side (X) for local controls
#define LC_LEFT_X        150

// Spacing between label text Y and controls below it
#define LC_LBL_DY        20

// Spacing between row of controls and labels for next row below
#define LC_CTRL_DY       10

// Left Buttons leading to another screen
#define BTN_EXT_DY       52
#define BTN_EXT_SCREEN_X 20
#define BTN_EXT_WIDTH    100
#define BTN_EXT_HEIGHT   40

// Row 1 (Top) labels
#define LC_R1_LBL_Y   SETTING_TOP
#define LC_LBL_RI_X   LC_LEFT_X
#define LC_LBL_G_X    (LC_LEFT_X + 160)

// Row 1 Controls
#define LC_R1_CTRL_Y  (SETTING_TOP + LC_LBL_DY)
#define LC_DD_RI_X    LC_LEFT_X
#define LC_DD_RI_W    120
#define LC_DD_RI_H    50
#define LC_DD_G_X     (LC_LEFT_X + 160)
#define LC_DD_G_W     100
#define LC_DD_G_H     LC_DD_RI_H

// Row 2 labels
#define LC_R2_LBL_Y   (LC_R1_CTRL_Y + LC_DD_RI_H + LC_CTRL_DY)
#define LC_LBL_MR_X   LC_LEFT_X

// Row 2 controls
#define LC_R2_CTRL_Y  (LC_R2_LBL_Y + LC_LBL_DY)
#define LC_SW_MR_X    LC_LEFT_X
#define LC_SW_MR_W    80
#define LC_SW_MR_H    30
#define LC_BTN_MIN_X  (LC_LEFT_X + LC_SW_MR_W + 30)
#define LC_BTN_MIN_W  80
#define LC_BTN_MIN_H  LC_SW_MR_H
#define LC_BTN_MAX_X  (LC_BTN_MIN_X + LC_BTN_MIN_W + 30)
#define LC_BTN_MAX_W  80
#define LC_BTN_MAX_H  LC_SW_MR_H

// Row 3 labels
#define LC_R3_LBL_Y   (LC_R2_CTRL_Y + LC_SW_MR_H + LC_CTRL_DY)
#define LC_LBL_EM_X   LC_LEFT_X

// Row 3 controls
#define LC_R3_CTRL_Y  (LC_R3_LBL_Y + LC_LBL_DY)
#define LC_BTN_EM_X   LC_LEFT_X
#define LC_BTN_EM_W   80
#define LC_BTN_EM_H   30
#define LC_BTN_LU_X   (LC_BTN_EM_X + LC_BTN_EM_W + 30)
#define LC_BTN_LU_W   80
#define LC_BTN_LU_H   LC_BTN_EM_H

// Row 4 labels
#define LC_R4_LBL_Y   (LC_R3_CTRL_Y + LC_BTN_EM_H + LC_CTRL_DY)
#define LC_LBL_MET_X  LC_LEFT_X
#define LC_LBL_BR_X   (LC_LBL_MET_X + 110)

// Row 4 controls
#define LC_R4_CTRL_Y  (LC_R4_LBL_Y + LC_LBL_DY)
#define LC_SW_MET_X   LC_LEFT_X
#define LC_SW_MET_W   80
#define LC_SW_MET_H   30
#define LC_SL_BR_X    (LC_SW_MET_X + LC_SW_MET_W + 30)
#define LC_SL_BR_W    180
#define LC_SL_BR_H    LC_SW_MET_H



// Control value limits
#define EMISSIVITY_BTN_MIN 1
#define EMISSIVITY_BTN_MAX 100
#define EMISSIVITY_LEP_MIN 82
#define EMISSIVITY_LEP_MAX 8192

// Manual Range
#define RANGE_BTN_MIN      (-273)
#define RANGE_BTN_MAX      500



//
// Settings GUI Screen API
//
lv_obj_t* gui_screen_settings_create();
void gui_screen_settings_set_active(bool en);


#endif /* GUI_SCREEN_SETTINGS_H */