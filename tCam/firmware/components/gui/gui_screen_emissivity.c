/*
 * Emissivity Lookup GUI screen related functions, callbacks and event handlers
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
#include "gui_screen_emissivity.h"
#include "gui_task.h"
#include "esp_system.h"
#include "gui_utilities.h"
#include "lepton_utilities.h"
#include "ps_utilities.h"
#include "emissivity_table.h"
#include "system_config.h"
#include "lv_conf.h"


//
// Emissivity Lookup GUI Screen constants
//

// Table content layout
#define EMISSIVITY_NUM_COLS  2
#define EMISSIVITY_NUM_ROWS  (NUM_EMISSIVITY_ENTRIES / EMISSIVITY_NUM_COLS)

//
// Emissivity Lookup GUI Screen variables
//

// LVGL objects
static lv_obj_t* emissivity_screen;
static lv_obj_t* lbl_emissivity_title;
static lv_obj_t* lbl_emissivity_value;
static lv_obj_t* btn_emissivity_exit;
static lv_obj_t* btn_emissivity_exit_label;
static lv_obj_t* btn_emissivity_save;
static lv_obj_t* btn_emissivity_save_label;
static lv_obj_t* page_tbl_page_scroll;
static lv_obj_t* tbl_emissivity;

// Selected cell styles
static lv_style_t tbl_sel_style;

// Screen state
static int cur_val;                       // -1 = nothing selected
static int new_val;
static uint16_t tbl_w;                    // Determined by scrolling page
static int prev_row;                      // Used to deselect previous entry
static int prev_col;



//
// Emissivity Lookup GUI Screen internal function forward declarations
//
static void initialize_screen_values();
static void initialize_emissivity_table();
static void update_emissivity_value();
static void cb_btn_exit(lv_obj_t * btn, lv_event_t event);
static void cb_btn_save(lv_obj_t * btn, lv_event_t event);
static void cb_tbl_emissivity(lv_obj_t * obj, lv_event_t event);



/**
 * Create the Emissivity Lookup screen, its graphical objects and link necessary callbacks
 */
lv_obj_t* gui_screen_emissivity_create()
{
	emissivity_screen = lv_obj_create(NULL, NULL);
	lv_obj_set_size(emissivity_screen, LV_HOR_RES_MAX, LV_VER_RES_MAX);
	
	// Create the graphical elements for this screen
	//
	// Screen Title using a larger font (centered)
	lbl_emissivity_title = lv_label_create(emissivity_screen, NULL);
	static lv_style_t lbl_emissivity_title_style;
	lv_style_copy(&lbl_emissivity_title_style, gui_st.gui_theme->style.bg);
	lbl_emissivity_title_style.text.font = &lv_font_roboto_22;
	lv_label_set_style(lbl_emissivity_title, LV_LABEL_STYLE_MAIN, &lbl_emissivity_title_style);
	gui_print_static_centered_text(lbl_emissivity_title, E_TITLE_LBL_X, E_TITLE_LBL_Y, "Emissivity Lookup");
	
	// Current selected emissivity value (centered)
	lbl_emissivity_value = lv_label_create(emissivity_screen, NULL);
	
	// Exit button
	btn_emissivity_exit = lv_btn_create(emissivity_screen, NULL);
	lv_obj_set_pos(btn_emissivity_exit, E_EXIT_BTN_X, E_EXIT_BTN_Y);
	lv_obj_set_size(btn_emissivity_exit, E_EXIT_BTN_W, E_EXIT_BTN_H);
	lv_obj_set_event_cb(btn_emissivity_exit, cb_btn_exit);
	btn_emissivity_exit_label = lv_label_create(btn_emissivity_exit, NULL);
	lv_label_set_static_text(btn_emissivity_exit_label, LV_SYMBOL_CLOSE);
	
	// Save button
	btn_emissivity_save = lv_btn_create(emissivity_screen, NULL);
	lv_obj_set_pos(btn_emissivity_save, E_SAVE_BTN_X, E_SAVE_BTN_Y);
	lv_obj_set_size(btn_emissivity_save, E_SAVE_BTN_W, E_SAVE_BTN_H);
	lv_obj_set_event_cb(btn_emissivity_save, cb_btn_save);
	btn_emissivity_save_label = lv_label_create(btn_emissivity_save, NULL);
	lv_label_set_static_text(btn_emissivity_save_label, LV_SYMBOL_OK);
	
	// Scrollable background page for Emissivity Table with no internal body padding
	page_tbl_page_scroll = lv_page_create(emissivity_screen, NULL);
	static lv_style_t page_bg_style;
	lv_style_copy(&page_bg_style, gui_st.gui_theme->style.bg);
	lv_obj_set_pos(page_tbl_page_scroll, E_TBL_PAGE_X, E_TBL_PAGE_Y);
	lv_obj_set_size(page_tbl_page_scroll, E_TBL_PAGE_W, E_TBL_PAGE_H);
	page_bg_style.body.padding.top = 0;
	page_bg_style.body.padding.bottom = 0;
	page_bg_style.body.padding.left = 0;
	page_bg_style.body.padding.right = 0;
	page_bg_style.body.border.width = 0;
	lv_page_set_style(page_tbl_page_scroll, LV_PAGE_STYLE_BG, &page_bg_style);
	lv_obj_set_event_cb(page_tbl_page_scroll, cb_tbl_emissivity);
	tbl_w = lv_page_get_fit_width(page_tbl_page_scroll);
	
	// Emissivity Table
	tbl_emissivity = lv_table_create(page_tbl_page_scroll, NULL);
	lv_table_set_col_cnt(tbl_emissivity, EMISSIVITY_NUM_COLS);
    lv_table_set_row_cnt(tbl_emissivity, EMISSIVITY_NUM_ROWS);
    lv_obj_set_pos(tbl_emissivity, (E_TBL_PAGE_W - tbl_w)/2, 0);
    
    // Selected cell style
    lv_style_copy(&tbl_sel_style, gui_st.gui_theme->style.table.cell);
    tbl_sel_style.body.main_color = lv_color_hsv_to_rgb(GUI_THEME_HUE, 75, 50);
    tbl_sel_style.body.grad_color = lv_color_hsv_to_rgb(GUI_THEME_HUE, 75, 50);
    lv_table_set_style(tbl_emissivity, LV_TABLE_STYLE_CELL2, &tbl_sel_style);
	
	initialize_screen_values();
		
	return emissivity_screen;
}


/**
 * Tell this screen if it is newly active or not
 * (activating a screen (re)initializes its display)
 */
void gui_screen_emissivity_set_active(bool en)
{
	if (en) {
		new_val = cur_val;
		update_emissivity_value();
	} else {
		// Deselect any currently selected cells
		if (prev_row != -1) {
			lv_table_set_cell_type(tbl_emissivity, prev_row, prev_col, 1);
			prev_row = -1;
			prev_col = -1;
		}
	}
}



//
// Emissivity Lookup GUI Screen internal functions
//

static void initialize_screen_values()
{
	// Initialize displayed on-screen values
	cur_val = -1;
	new_val = -1;
	prev_row = -1;
	prev_col = -1;
	update_emissivity_value();
	initialize_emissivity_table();
}


static void initialize_emissivity_table()
{
	int r, c;
	
	for (c=0; c<EMISSIVITY_NUM_COLS; c++) {
		lv_table_set_col_width(tbl_emissivity, c, tbl_w/EMISSIVITY_NUM_COLS - 1);
		for (r=0; r<EMISSIVITY_NUM_ROWS; r++) {
			lv_table_set_cell_value(tbl_emissivity, r, c, emissivity_table[r + (c*EMISSIVITY_NUM_ROWS)].item);
		}
	}
}


static void update_emissivity_value()
{
	static char buf[40];    // Static buffer space for lv_label_static_text - "<emissivity> - NN%"
	
	if ((new_val >= 0) && (new_val < NUM_EMISSIVITY_ENTRIES)) {
		sprintf(buf, "%s - %d%%", emissivity_table[new_val].item, emissivity_table[new_val].e);
		gui_print_static_centered_text(lbl_emissivity_value, 240, 40, buf);
	} else {
		buf[0] = 0;
		lv_label_set_static_text(lbl_emissivity_value, buf);
	}
}


static void cb_btn_exit(lv_obj_t * btn, lv_event_t event)
{
	if (event == LV_EVENT_CLICKED) {
		gui_set_screen(GUI_SCREEN_SETTINGS);
	}
}


static void cb_btn_save(lv_obj_t * btn, lv_event_t event)
{
	lep_config_t* lep_stP = lep_get_lep_st();
	
	if (event == LV_EVENT_CLICKED) {
		if (new_val >= 0) {
			cur_val = new_val;
			if (lep_stP->emissivity != emissivity_table[cur_val].e) {
				lep_stP->emissivity = emissivity_table[cur_val].e;
				lepton_emissivity(lep_stP->emissivity);
				ps_set_lep_state(lep_stP);
			}
		}
		gui_set_screen(GUI_SCREEN_SETTINGS);
	}
}


static void cb_tbl_emissivity(lv_obj_t * obj, lv_event_t event)
{
	int row_h, y_off;
	int r, c;
	int val;
	lv_indev_t* touch;          // Input device
	lv_point_t cur_point;
	
	if (event == LV_EVENT_CLICKED) {
		touch = lv_indev_get_act();
		lv_indev_get_point(touch, &cur_point);
		// Determine the column directly from the touch x coordinate
		c = cur_point.x / (LV_HOR_RES_MAX / EMISSIVITY_NUM_COLS);
		
		// The row is trickier since the table object may be scrolled.  We dig into its
		// object to find where it is relative to the screen and then figure which row
		// the click is in.
		row_h = lv_obj_get_height(tbl_emissivity) / EMISSIVITY_NUM_ROWS;
		y_off = cur_point.y - tbl_emissivity->coords.y1;
		if (y_off < 0) y_off = 0;
		r = y_off / row_h;
		val = r + c*EMISSIVITY_NUM_ROWS;
		if (val >= NUM_EMISSIVITY_ENTRIES) val = NUM_EMISSIVITY_ENTRIES - 1;
		if (val == new_val) {
			// Delete displayed value if double-clicked
			new_val = -1;
			
			// De-highlight the cell
			lv_table_set_cell_type(tbl_emissivity, r, c, 1);
		} else {
			new_val = val;
			
			// Highlight the selected cell
			lv_table_set_cell_type(tbl_emissivity, r, c, 2);
			
			// De-highlight any previously selected cell
			if ((prev_row != -1) && ((prev_row != r) || (prev_col != c))) {
				lv_table_set_cell_type(tbl_emissivity, prev_row, prev_col, 1);
			}
			prev_row = r;
			prev_col = c;
		}
		
		lv_obj_invalidate(tbl_emissivity);
		update_emissivity_value();
	}
}

