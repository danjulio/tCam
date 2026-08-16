/*
 * Renderers for lepton images, spot meter and min/max markers
 *
 * Copyright 2020, 2023 Dan Julio
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
#ifndef RENDER_H
#define RENDER_H

#include <stdbool.h>
#include <stdint.h>
#include "gui_utilities.h"
#include "lepton_utilities.h"
#include "sys_utilities.h"


//
// Render Constants
//

// Image buffer dimensions
#define IMG_BUF_MULT_FACTOR 2
#define IMG_BUF_WIDTH       (IMG_BUF_MULT_FACTOR * LEP_WIDTH)
#define IMG_BUF_HEIGHT      (IMG_BUF_MULT_FACTOR * LEP_HEIGHT)

// Spot meter
#define IMG_SPOT_MIN_SIZE   10

// Min/Max markers
#define IMG_MM_MARKER_SIZE  10

// Linear Interpolation Scale Factors
//  DS = Dual Source Pixel case (SF_DS is typically 2 or 3)
//  QS = Quad Source Pixel case (SF_QS is typically 3 or 5)
//
#define SF_DS 3
#define SF_QS 5
#define DIV_DS (SF_DS + 1)
#define DIV_QS (SF_QS + 3)


//
// Render API
//
void render_lep_data(lep_buffer_t* lep, uint16_t* img, gui_state_t* g);
void render_spotmeter(lep_buffer_t* lep, uint16_t* img);
void render_min_max_markers(lep_buffer_t* lep, uint16_t* img);

#endif /* RENDER_H */