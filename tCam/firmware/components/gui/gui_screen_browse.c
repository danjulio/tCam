/*
 * Browse Files GUI screen related functions, callbacks and event handlers
 *
 * Copyright 2020-2021 Dan Julio
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
#include "gui_screen_browse.h"
#include "app_task.h"
#include "file_task.h"
#include "gui_task.h"
#include "gui_screen_view.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "file_utilities.h"
#include "gui_utilities.h"
#include "sys_utilities.h"
#include "system_config.h"
#include "lv_conf.h"
#include <string.h>



//
// Browse Files GUI Screen internal constants
//

// Action to take on positive confirmation from messagebox when a delete or format button pressed
#define CONFIRM_ACTION_DIR    0
#define CONFIRM_ACTION_FILE   1
#define CONFIRM_ACTION_FORMAT 2



//
// Browse Files GUI Screen variables
//

// LVGL objects
static lv_obj_t* browse_screen;
static lv_obj_t* lbl_browse_title;
static lv_obj_t* btn_browse_exit;
static lv_obj_t* btn_browse_exit_label;

// Tables
static lv_obj_t* lbl_tbl_dir;
static lv_obj_t* lbl_tbl_file;
static lv_obj_t* page_tbl_dir_scroll;
static lv_obj_t* page_tbl_file_scroll;
static lv_obj_t* tbl_dir_browse;              // Created and destroyed dynamically
static lv_obj_t* tbl_file_browse;             // Created and destroyed dynamically

// Buttons
static lv_obj_t* btn_view;
static lv_obj_t* btn_view_label;
static lv_obj_t* btn_delete;
static lv_obj_t* btn_delete_label;
static lv_obj_t* btn_format;
static lv_obj_t* btn_format_label;

// Storage Information
static lv_obj_t* lbl_num_files;
static lv_obj_t* lbl_freespace;

// Selected cell styles
static lv_style_t tbl_sel_style;

// Status update task
static lv_task_t* task_update;


// Screen state
static bool card_present;
static bool browsing_files;               // Set when displaying files
static bool dir_selected;                 // Set when a directory name has been selected
static bool file_selected;                // Set when a file name has been selected
static uint16_t tbl_dir_w;                // Determined by scrolling page
static uint16_t tbl_file_w;
static int prev_tbl_dir_row;
static int prev_tbl_file_row;
static int confirmation_action;           // Set to indicate what action to perform on confirmation
static char dir_name[FILE_NAME_LEN];      // Current directory name
static char file_name[FILE_NAME_LEN];     // Current directory name



//
// Browse Files GUI Screen internal function forward declarations
//
static void initialize_screen_values();
static void card_present_update_task(lv_task_t * task);
static void update_card_present_state(bool present);
static void update_browse_button_state();
static void update_format_button_state();
static void update_info();
static void cb_btn_exit(lv_obj_t * btn, lv_event_t event);
static void cb_btn_view(lv_obj_t * obj, lv_event_t event);
static void cb_btn_delete(lv_obj_t * obj, lv_event_t event);
static void cb_btn_format(lv_obj_t * obj, lv_event_t event);
static void cb_tbl_dir(lv_obj_t * obj, lv_event_t event);
static void cb_tbl_file(lv_obj_t * obj, lv_event_t event);
static int get_table_row(lv_obj_t * obj);



/**
 * Create the Browse Files screen, its graphical objects and link necessary callbacks
 */
lv_obj_t* gui_screen_browse_create()
{
	browse_screen = lv_obj_create(NULL, NULL);
	lv_obj_set_size(browse_screen, LV_HOR_RES_MAX, LV_VER_RES_MAX);
	
	// Create the graphical elements for this screen
	//
	// Screen Title using a larger font (centered)
	lbl_browse_title = lv_label_create(browse_screen, NULL);
	static lv_style_t lbl_browse_title_style;
	lv_style_copy(&lbl_browse_title_style, gui_st.gui_theme->style.bg);
	lbl_browse_title_style.text.font = &lv_font_roboto_22;
	lv_label_set_style(lbl_browse_title, LV_LABEL_STYLE_MAIN, &lbl_browse_title_style);
	gui_print_static_centered_text(lbl_browse_title, B_TITLE_LBL_X, B_TITLE_LBL_Y, "Manage Storage");
	
	// Exit button
	btn_browse_exit = lv_btn_create(browse_screen, NULL);
	lv_obj_set_pos(btn_browse_exit, B_EXIT_BTN_X, B_EXIT_BTN_Y);
	lv_obj_set_size(btn_browse_exit, B_EXIT_BTN_W, B_EXIT_BTN_H);
	lv_obj_set_event_cb(btn_browse_exit, cb_btn_exit);
	btn_browse_exit_label = lv_label_create(btn_browse_exit, NULL);
	lv_label_set_static_text(btn_browse_exit_label, LV_SYMBOL_CLOSE);
	
	// Directory Table Label
	lbl_tbl_dir = lv_label_create(browse_screen, NULL);
	lv_label_set_long_mode(lbl_tbl_dir, LV_LABEL_LONG_BREAK);
	lv_label_set_align(lbl_tbl_dir, LV_LABEL_ALIGN_CENTER);
	lv_obj_set_pos(lbl_tbl_dir, B_DIR_LBL_X, B_DIR_LBL_Y);
	lv_obj_set_width(lbl_tbl_dir, B_TBL_PAGE_W);
	lv_label_set_static_text(lbl_tbl_dir, "Folders");
	
	// File Table Label
	lbl_tbl_file = lv_label_create(browse_screen, NULL);
	lv_label_set_long_mode(lbl_tbl_file, LV_LABEL_LONG_BREAK);
	lv_label_set_align(lbl_tbl_file, LV_LABEL_ALIGN_CENTER);
	lv_obj_set_pos(lbl_tbl_file, B_FILE_LBL_X, B_FILE_LBL_Y);
	lv_obj_set_width(lbl_tbl_file, B_TBL_PAGE_W);
	lv_label_set_static_text(lbl_tbl_file, "Files");
	
	// Style for scrollable background page
	static lv_style_t page_bg_style;
	lv_style_copy(&page_bg_style, gui_st.gui_theme->style.bg);
	page_bg_style.body.padding.top = 0;
	page_bg_style.body.padding.bottom = 0;
	page_bg_style.body.padding.left = 0;
	page_bg_style.body.padding.right = 0;
	page_bg_style.body.border.width = 0;
	
	// Scrollable background page for Directory List Table with no internal body padding
	page_tbl_dir_scroll = lv_page_create(browse_screen, NULL);
	lv_obj_set_pos(page_tbl_dir_scroll, B_DIR_TBL_PAGE_X, B_DIR_TBL_PAGE_Y);
	lv_obj_set_size(page_tbl_dir_scroll, B_TBL_PAGE_W, B_DIR_TBL_PAGE_H);
	lv_page_set_style(page_tbl_dir_scroll, LV_PAGE_STYLE_BG, &page_bg_style);
	lv_obj_set_event_cb(page_tbl_dir_scroll, cb_tbl_dir);
	tbl_dir_w = lv_page_get_fit_width(page_tbl_dir_scroll);
	
	// Scrollable background page for File List Table with no internal body padding
	page_tbl_file_scroll = lv_page_create(browse_screen, NULL);
	lv_obj_set_pos(page_tbl_file_scroll, B_FILE_TBL_PAGE_X, B_FILE_TBL_PAGE_Y);
	lv_obj_set_size(page_tbl_file_scroll, B_TBL_PAGE_W, B_FILE_TBL_PAGE_H);
	lv_page_set_style(page_tbl_file_scroll, LV_PAGE_STYLE_BG, &page_bg_style);
	lv_obj_set_event_cb(page_tbl_file_scroll, cb_tbl_file);
	tbl_file_w = lv_page_get_fit_width(page_tbl_file_scroll);
	
	// Selected cell style
    lv_style_copy(&tbl_sel_style, gui_st.gui_theme->style.table.cell);
    tbl_sel_style.body.main_color = lv_color_hsv_to_rgb(GUI_THEME_HUE, 75, 50);
    tbl_sel_style.body.grad_color = lv_color_hsv_to_rgb(GUI_THEME_HUE, 75, 50);
    
    // View file button
    btn_view = lv_btn_create(browse_screen, NULL);
	lv_obj_set_pos(btn_view, B_VIEW_BTN_X, B_VIEW_BTN_Y);
	lv_obj_set_size(btn_view, B_VIEW_BTN_W, B_VIEW_BTN_H);
	lv_obj_set_event_cb(btn_view, cb_btn_view);
	btn_view_label = lv_label_create(btn_view, NULL);
	lv_label_set_recolor(btn_view_label, true);
	
	// Delete button
	btn_delete = lv_btn_create(browse_screen, NULL);
	lv_obj_set_pos(btn_delete, B_DEL_BTN_X, B_DEL_BTN_Y);
	lv_obj_set_size(btn_delete, B_DEL_BTN_W, B_DEL_BTN_H);
	lv_obj_set_event_cb(btn_delete, cb_btn_delete);
	btn_delete_label = lv_label_create(btn_delete, NULL);
	lv_label_set_recolor(btn_delete_label, true);
	
	// Format button
	btn_format = lv_btn_create(browse_screen, NULL);
	lv_obj_set_pos(btn_format, B_FMT_BTN_X, B_FMT_BTN_Y);
	lv_obj_set_size(btn_format, B_FMT_BTN_W, B_FMT_BTN_H);
	lv_obj_set_event_cb(btn_format, cb_btn_format);
	btn_format_label = lv_label_create(btn_format, NULL);
	lv_label_set_recolor(btn_format_label, true);
	
	// Number of files label
	lbl_num_files = lv_label_create(browse_screen, NULL);
	lv_obj_set_pos(lbl_num_files, B_NUM_FILE_LBL_X, B_NUM_FILE_LBL_Y);
	lv_obj_set_width(lbl_num_files, B_NUM_FILE_LBL_W);
	
	// Freespace label
	lbl_freespace = lv_label_create(browse_screen, NULL);
	lv_obj_set_pos(lbl_freespace, B_FREE_LBL_X, B_FREE_LBL_Y);
	lv_obj_set_width(lbl_freespace, B_FREE_LBL_W);
	
	initialize_screen_values();
		
	return browse_screen;
}


/**
 * Tell this screen if it is newly active or not
 * (activating a screen (re)initializes its display)
 */
void gui_screen_browse_set_active(bool en)
{
	if (en) {
		// Initialize based on card presence
		update_card_present_state(file_card_present());
		
		// Card detection update sub-task runs twice per second
		task_update = lv_task_create(card_present_update_task, 500, LV_TASK_PRIO_LOW, NULL);
	} else {
		// Delete the table objects if they exist
		if (tbl_dir_browse != NULL) {
			(void) lv_obj_del(tbl_dir_browse);
			tbl_dir_browse = NULL;
		}
		if (tbl_file_browse != NULL) {
			(void) lv_obj_del(tbl_file_browse);
			tbl_file_browse = NULL;
		}
		
		// Delete the update task if it exists
		if (task_update != NULL) {
			lv_task_del(task_update);
			task_update = NULL;
		}
	}
}


/**
 * Update the selected table with a list of names from file_task
 */
void gui_screen_browse_update_list()
{
	char c;
	char* names;
	char filename[FILE_NAME_LEN];  // Larger than longest expected name
	int i, r;
	int num_names;
	int dir_num;
	lv_obj_t* tbl;
	
	// Get a pointer to the comma separated list of names
	names = file_get_catalog(FILE_REQ_SRC_GUI, &num_names, &dir_num);
	
	// Create a new table on the appropriate scrollable page
	tbl = lv_table_create(browsing_files ? page_tbl_file_scroll : page_tbl_dir_scroll, NULL);
	lv_table_set_col_width(tbl, 0, tbl_dir_w);
	lv_table_set_col_cnt(tbl, 1);
    lv_table_set_row_cnt(tbl, num_names);
    lv_obj_set_pos(tbl, (B_TBL_PAGE_W - tbl_dir_w)/2, 0);
	lv_table_set_style(tbl, LV_TABLE_STYLE_CELL2, &tbl_sel_style);
	
	// Convert list of names into table entries
	r = 0;
	while (num_names--) {
		i = 0;
		for (;;) {
			c = *names++;
			if ((c == ',') || (i == (FILE_NAME_LEN-1))) {
				filename[i] = 0;
				break;
			} else {
				filename[i] = c;
			}
			i++;
		}
		lv_table_set_cell_value(tbl, r, 0, filename);
		lv_table_set_cell_crop(tbl, r, 0, true);
		r++;
	}
	
	// Select the table to update
	if (browsing_files) {
		// Update file table - delete any existing table first
		if (tbl_file_browse != NULL) {
			// Force the scrollable page to scroll to the top of the existing
			// table before replacing it with the new page
			lv_page_scroll_ver(page_tbl_file_scroll, lv_obj_get_height(tbl_file_browse));
			(void) lv_obj_del(tbl_file_browse);
		}
		prev_tbl_file_row = -1;
		tbl_file_browse = tbl;
		file_selected = false;
	} else {
		// Update directory table
		prev_tbl_dir_row = -1;
		tbl_dir_browse = tbl;
		
		// Next update will be a file list
		browsing_files = true;
		dir_selected = false;
	}
	
	// Update control state
	update_browse_button_state();
}


/**
 * Set the btn pressed in a messagebox - used to trigger activity in response
 * to specific buttons
 */
void gui_screen_browse_set_msgbox_btn(uint16_t btn)
{
	// We're called from the message box displayed when the user clicks btn_delete
	// or btn_format.  We only act if they clicked to perform the operation set in
	// confirmation_action.
	if (btn == GUI_MSG_BOX_BTN_AFFIRM) {
		switch (confirmation_action) {
			case CONFIRM_ACTION_DIR:
				// Initiate deletion of the previously set directory
				xTaskNotify(task_handle_app, APP_NOTIFY_GUI_DEL_DIR_MASK, eSetBits);
				break;
			
			case CONFIRM_ACTION_FILE:
				// Initiate deletion of the previously set file
				xTaskNotify(task_handle_app, APP_NOTIFY_GUI_DEL_FILE_MASK, eSetBits);
				break;
			
			case CONFIRM_ACTION_FORMAT:
				// Initiate formatting the card
				xTaskNotify(task_handle_app, APP_NOTIFY_GUI_FORMAT_MASK, eSetBits);
				break;
		}
	}
}


/**
 * Called after the action specified by confirmation_action has been successfully completed.
 * Update the screen if there are directories/files to display, otherwise return to the
 * settings screen.
 */
void gui_screen_browse_update_after_delete()
{
	bool last_file;
	
	if (confirmation_action == CONFIRM_ACTION_FILE) {
		// Deleted a file: Update the file list if there are still files in the
		// directory.  Otherwise, update the directory list (and blank the file list).
		//
		// Determine if the file that was deleted was the last one in a directory
		last_file = lv_table_get_row_cnt(tbl_file_browse) == 1;
		
		// Delete the existing file list
		if (tbl_file_browse != NULL) {
			(void) lv_obj_del(tbl_file_browse);
			tbl_file_browse = NULL;
		}
		
		if (last_file) {
			// Also delete the existing directory list
			if (tbl_dir_browse != NULL) {
				(void) lv_obj_del(tbl_dir_browse);
				tbl_dir_browse = NULL;
			}
			
			// Request a new list of directories to update
			browsing_files = false;
			dir_selected = false;
			file_selected = false;
			file_set_catalog_index(FILE_REQ_SRC_GUI, -1);
			xTaskNotify(task_handle_file, FILE_NOTIFY_GUI_GET_CATALOG_MASK, eSetBits);
		} else {
			// Request a new list of files to update
			file_selected = false;
			file_set_catalog_index(FILE_REQ_SRC_GUI, prev_tbl_dir_row);
			xTaskNotify(task_handle_file, FILE_NOTIFY_GUI_GET_CATALOG_MASK, eSetBits);
		}
	} else {
		// Format or deleted a directory.  Request the directory list (which shouldn't
		// be returned for a formatted media).
		//
		// Delete the existing directory list
		if (tbl_dir_browse != NULL) {
			(void) lv_obj_del(tbl_dir_browse);
			tbl_dir_browse = NULL;
		}
		
		// Also delete the existing file list
		if (tbl_file_browse != NULL) {
			(void) lv_obj_del(tbl_file_browse);
			tbl_file_browse = NULL;
		}
		
		// Request a new list of directories to update
		browsing_files = false;
		dir_selected = false;
		file_selected = false;
		file_set_catalog_index(FILE_REQ_SRC_GUI, -1);
		xTaskNotify(task_handle_file, FILE_NOTIFY_GUI_GET_CATALOG_MASK, eSetBits);
	}
		
	// Update controls state
	update_browse_button_state();
	
	// Update information
	update_info();
}



//
// Browse Files GUI Screen internal functions
//

static void initialize_screen_values()
{
	// Start out with no tables, ready to display directory list
	card_present = false;
	tbl_dir_browse = NULL;
	tbl_file_browse = NULL;
	browsing_files = false;
	dir_selected = false;
	file_selected = false;
	
	// Initial control value
	update_browse_button_state();
	update_info();
	
	// No task yet
	task_update = NULL;
}


void card_present_update_task(lv_task_t * task)
{
	bool present;
	
	present = file_card_present();
	
	if (present != card_present) {
		update_card_present_state(present);
	}
}

static void update_card_present_state(bool present)
{
	card_present = present;
	
	browsing_files = false;
	dir_selected = false;
	file_selected = false;
	
	if (card_present) {
		// Request list of directories to update
		file_set_catalog_index(FILE_REQ_SRC_GUI, -1);
		xTaskNotify(task_handle_file, FILE_NOTIFY_GUI_GET_CATALOG_MASK, eSetBits);
	} else {
		// Delete any existing lists
		if (tbl_file_browse != NULL) {
			(void) lv_obj_del(tbl_file_browse);
			tbl_file_browse = NULL;
		}
		
		if (tbl_dir_browse != NULL) {
			(void) lv_obj_del(tbl_dir_browse);
			tbl_dir_browse = NULL;
		}
	}
	
	// Update controls state
	update_browse_button_state();
	update_format_button_state();
	
	// Update information
	update_info();
}


static void update_browse_button_state()
{
	static char view_buf[13];   // Statically allocated, big enough for recolor symbol + symbol '#XXXXXX sss#0'
	static char del_buf[13];
	
	if (file_selected) {
		sprintf(view_buf, "#FFFFFF " LV_SYMBOL_IMAGE "#");
		lv_obj_set_click(btn_view, true);
	} else {
		sprintf(view_buf, "#808080 " LV_SYMBOL_IMAGE "#");
		lv_obj_set_click(btn_view, false);
	}
	lv_label_set_static_text(btn_view_label, view_buf);
	
	if (dir_selected) {
		sprintf(del_buf, "#FFFFFF " LV_SYMBOL_TRASH "#");
		lv_obj_set_click(btn_delete, true);
	} else {
		sprintf(del_buf, "#808080 " LV_SYMBOL_TRASH "#");
		lv_obj_set_click(btn_delete, false);
	}
	lv_label_set_static_text(btn_delete_label, del_buf);
}


static void update_format_button_state()
{
	static char view_buf[13];   // Statically allocated, big enough for recolor symbol + symbol '#XXXXXX sss#0'
	
	if (card_present) {
		sprintf(view_buf, "#FFFFFF " LV_SYMBOL_SD_CARD "#");
		lv_obj_set_click(btn_format, true);
	} else {
		sprintf(view_buf, "#808080 " LV_SYMBOL_SD_CARD "#");
		lv_obj_set_click(btn_format, false);
	}
	lv_label_set_static_text(btn_format_label, view_buf);
}


static void update_info()
{
	static char num_files_buf[13];  // Statically allocated for "nnnnnn Files"
	static char freespace_buf[14];  // Statically allocated for "XXXXX MB Free"
	int num_files;
	int mb_free;
	
	num_files = file_get_num_files();
	if (num_files == 1) {
		sprintf(num_files_buf, "1 File");
	} else {
		sprintf(num_files_buf, "%d Files", num_files);
	}
	lv_label_set_static_text(lbl_num_files, num_files_buf);
	
	if (card_present) {
		mb_free = (int) (file_get_storage_free() / (1000 * 1000));
		sprintf(freespace_buf, "%d MB Free", mb_free);
	} else {
		sprintf(freespace_buf, "No Media");
	}
	lv_label_set_static_text(lbl_freespace, freespace_buf);
}


static void cb_btn_exit(lv_obj_t * btn, lv_event_t event)
{
	if (event == LV_EVENT_CLICKED) {
		// Back to settings screen
		gui_set_screen(GUI_SCREEN_SETTINGS);
	}
}


static void cb_btn_view(lv_obj_t * obj, lv_event_t event)
{
	int file_index;
	
	if ((event == LV_EVENT_CLICKED) && file_selected) {
		file_index = file_get_abs_file_index(prev_tbl_dir_row, prev_tbl_file_row);
		
		if (file_index >= 0) {
			// Setup file to view
			gui_screen_view_set_file_info(file_index);
		
			// Switch to file view screen
			gui_set_screen(GUI_SCREEN_VIEW);
		}
	}
}


static void cb_btn_delete(lv_obj_t * obj, lv_event_t event)
{
	if ((event == LV_EVENT_CLICKED) && dir_selected) {
		if (file_selected) {
			confirmation_action = CONFIRM_ACTION_FILE;
			file_set_del_image(FILE_REQ_SRC_GUI, dir_name, file_name);
			// Make sure the user wants to delete the file - the button the user presses
			// on the messagebox will decide what we do in gui_screen_browse_set_msgbox_btn
			gui_preset_message_box_string("Are you sure you want to delete the file?", true);
			xTaskNotify(task_handle_gui, GUI_NOTIFY_MESSAGEBOX_MASK, eSetBits);
		} else {
			confirmation_action = CONFIRM_ACTION_DIR;
			file_set_del_dir(FILE_REQ_SRC_GUI, dir_name);
			gui_preset_message_box_string("Are you sure you want to delete the directory and its contents?", true);
			xTaskNotify(task_handle_gui, GUI_NOTIFY_MESSAGEBOX_MASK, eSetBits);
		}
	}
}


static void cb_btn_format(lv_obj_t * obj, lv_event_t event)
{
	if (event == LV_EVENT_CLICKED) {
		confirmation_action = CONFIRM_ACTION_FORMAT;
		gui_preset_message_box_string("Are you sure you want to format the card and erase all contents?", true);
		xTaskNotify(task_handle_gui, GUI_NOTIFY_MESSAGEBOX_MASK, eSetBits);
	}
}


static void cb_tbl_dir(lv_obj_t * obj, lv_event_t event)
{
	int row;
	
	if ((event == LV_EVENT_CLICKED) && (tbl_dir_browse != NULL)) {
		if (lv_table_get_row_cnt(tbl_dir_browse) > 0) {
			row = get_table_row(tbl_dir_browse);
			
			if (row != prev_tbl_dir_row) {
				// De-highlight any previously selected cell
				if (prev_tbl_dir_row != -1) {
					lv_table_set_cell_type(tbl_dir_browse, prev_tbl_dir_row, 0, 1);
				}
					
				// Highlight the selected cell
				lv_table_set_cell_type(tbl_dir_browse, row, 0, 2);
				prev_tbl_dir_row = row;
				
				// Request file list
				file_set_catalog_index(FILE_REQ_SRC_GUI, row);
				xTaskNotify(task_handle_file, FILE_NOTIFY_GUI_GET_CATALOG_MASK, eSetBits);
					
				// Get a copy of the selected directory name
				strcpy(dir_name, lv_table_get_cell_value(tbl_dir_browse, row, 0));
				
				// Note user has selected a directory
				dir_selected = true;
				
				// Update control state
				update_browse_button_state();
				
				// Finally force a table update for the highlights
				lv_obj_invalidate(obj);
			}
		}
	}
}


static void cb_tbl_file(lv_obj_t * obj, lv_event_t event)
{
	int row;
	
	if ((event == LV_EVENT_CLICKED) && (tbl_file_browse != NULL)) {
		if (lv_table_get_row_cnt(tbl_file_browse) > 0) {
			row = get_table_row(tbl_file_browse);
		
			if (row != prev_tbl_file_row) {
				// De-highlight any previously selected cell
				if (prev_tbl_file_row != -1) {
					lv_table_set_cell_type(tbl_file_browse, prev_tbl_file_row, 0, 1);
				}
				
				// Highlight the selected cell
				lv_table_set_cell_type(tbl_file_browse, row, 0, 2);
				prev_tbl_file_row = row;
				
				// Get a copy of the selected file name
				strcpy(file_name, lv_table_get_cell_value(tbl_file_browse, row, 0));
				
				// Note user has selected a file
				file_selected = true;
				
				// Update control state
				update_browse_button_state();
				
				// Finally force a table update for the highlights
				lv_obj_invalidate(obj);
			}
		}
	}
}


static int get_table_row(lv_obj_t * obj)
{
	int row_h, y_off, num_rows, sel_row;
	lv_indev_t* touch;          // Input device
	lv_point_t cur_point;
	
	// Get the touch coordinates
	touch = lv_indev_get_act();
	lv_indev_get_point(touch, &cur_point);
		
	// Determining the row is tricky since the table object may be scrolled.  We dig into
	// its object to find where it is relative to the screen and then figure which row
	// the click is in.
	num_rows = lv_table_get_row_cnt(obj);
	row_h = lv_obj_get_height(obj) / num_rows;
	y_off = cur_point.y - obj->coords.y1;
	if (y_off < 0) y_off = 0;
	sel_row = y_off / row_h;
	if (sel_row > (num_rows-1)) sel_row = num_rows - 1;
	
	return sel_row;
}
