/*
 * View Files GUI screen related functions, callbacks and event handlers
 *
 * Copyright 2020-2023 Dan Julio
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
#include "gui_screen_view.h"
#include "gui_screen_browse.h"
#include "app_task.h"
#include "file_task.h"
#include "gui_task.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "file_utilities.h"
#include "gui_utilities.h"
#include "lepton_utilities.h"
#include "sys_utilities.h"
#include "time_utilities.h"
#include "system_config.h"
#include "lv_conf.h"
#include "palettes.h"
#include "render.h"
#include <string.h>



//
// View GUI Screen private constants
//
#define VIEW_PB_ST_IDLE  0
#define VIEW_PB_ST_PAUSE 1
#define VIEW_PB_ST_PLAY  2
#define VIEW_PB_ST_DONE  3



//
// View GUI Screen variables
//

// LVGL objects
static lv_obj_t* view_screen;
static lv_obj_t* btn_back;
static lv_obj_t* btn_back_label;

// Header
static lv_obj_t* lbl_view_file;
static lv_obj_t* lbl_spot_temp;

// Colormap
static lv_obj_t* canvas_colormap;
static lv_style_t colormap_entry_style;
static lv_style_t colormap_temp_style;
static lv_style_t colormap_clear_temp_style;
static lv_style_t colormap_marker_style;

// Lepton Image
static lv_img_dsc_t lepton_img_dsc;
static lv_obj_t* img_view;

// Controls
static lv_obj_t* btn_browse;
static lv_obj_t* btn_browse_label;
static lv_obj_t* btn_delete;
static lv_obj_t* btn_delete_label;
static lv_obj_t* btn_range_mode;
static lv_obj_t* btn_range_mode_label;
static lv_obj_t* btn_play;
static lv_obj_t* btn_play_label;
static lv_obj_t* btn_prev;
static lv_obj_t* btn_prev_label;
static lv_obj_t* btn_next;
static lv_obj_t* btn_next_label;

// Bottom information area
static lv_obj_t* lbl_playback_info;

//
// Screen state
//
static bool image_valid;
static gui_state_t view_gui_st;

// Filesystem Information Structure (catalog) state
static int cur_file_index;                // Absolute file index of currently displayed image
static int next_file_index;               // Set when a file is deleted with the value to use on successful deletion
                                          //   ... -1 indicates no files to display after deletion

static directory_node_t* cur_dir_node;
static file_node_t* cur_file_node;

// Video playback state
static bool image_is_video;
static int video_st;
static int video_gui_buf_index;
static uint32_t video_len_msec;
static uint32_t video_cur_msec;


//
// View GUI Screen internal function forward declarations
//
static void initialize_screen_values();
static void set_colormap(int n);
static void set_spot_marker(int offset, bool erase);

static void update_colormap();
static void update_colormap_temps(lep_buffer_t* sys_bufP, bool init);
static void update_colormap_marker(lep_buffer_t* sys_bufP);
static void update_filename(bool init);
static void update_spot_temp(lep_buffer_t* sys_bufP, bool init);
static void update_range_mode_btn_label();
static void update_navigate_buttons();
static void update_play_button();
static void update_video_timestamps();

static void cb_lepton_image(lv_obj_t * img, lv_event_t event);
static void cb_colormap_canvas(lv_obj_t * btn, lv_event_t event);
static void cb_spot_temp(lv_obj_t * btn, lv_event_t event);
static void cb_btn_exit(lv_obj_t * btn, lv_event_t event);
static void cb_btn_browse(lv_obj_t * btn, lv_event_t event);
static void cb_btn_delete(lv_obj_t * btn, lv_event_t event);
static void cb_btn_range_mode(lv_obj_t * btn, lv_event_t event);
static void cb_btn_play(lv_obj_t * btn, lv_event_t event);
static void cb_btn_navigate(lv_obj_t * btn, lv_event_t event);

static bool get_file_info();
static void request_file();
static bool select_next_file();
static bool select_prev_file();
static void stop_video();


//
// Main GUI Screen API
//

/**
 * Create the main screen, its graphical objects and link necessary callbacks
 */
lv_obj_t* gui_screen_view_create()
{
	view_screen = lv_obj_create(NULL, NULL);
	lv_obj_set_size(view_screen, LV_HOR_RES_MAX, LV_VER_RES_MAX);
	
	// Create the graphical elements for this screen
	//
	// Header
	lbl_view_file = lv_label_create(view_screen, NULL);
	lv_obj_set_pos(lbl_view_file, VIEW_HEADER_LBL_X, VIEW_HEADER_LBL_Y);
	
	// Lepton image Color map area
	canvas_colormap = lv_canvas_create(view_screen, NULL);
	lv_canvas_set_buffer(canvas_colormap, gui_cmap_canvas_buffer, VIEW_CMAP_CANVAS_WIDTH, VIEW_CMAP_CANVAS_HEIGHT, LV_IMG_CF_TRUE_COLOR);
	lv_obj_set_pos(canvas_colormap, VIEW_CMAP_X, VIEW_CMAP_Y);
	lv_obj_set_click(canvas_colormap, true);
	lv_obj_set_event_cb(canvas_colormap, cb_colormap_canvas);
	
	lv_style_copy(&colormap_entry_style, &lv_style_plain);
	colormap_entry_style.line.width = 1;
	colormap_entry_style.line.opa = LV_OPA_COVER;
	
	lv_style_copy(&colormap_temp_style, gui_st.gui_theme->style.bg);
	
	lv_style_copy(&colormap_clear_temp_style, &lv_style_plain);
	colormap_clear_temp_style.body.main_color = VIEW_PALETTE_BG_COLOR;
	colormap_clear_temp_style.body.grad_color = VIEW_PALETTE_BG_COLOR;
	
	lv_style_copy(&colormap_marker_style, gui_st.gui_theme->style.bg);
	
	// Lepton image data structure
	lepton_img_dsc.header.always_zero = 0;
	lepton_img_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
	lepton_img_dsc.header.w = LEP_IMG_WIDTH;
	lepton_img_dsc.header.h = LEP_IMG_HEIGHT;
	lepton_img_dsc.data_size = LEP_IMG_WIDTH * LEP_IMG_HEIGHT * 2;
	lepton_img_dsc.data = (uint8_t*) gui_lep_canvas_buffer;

	// Lepton Image Area
	img_view = lv_img_create(view_screen, NULL);
	lv_img_set_src(img_view, &lepton_img_dsc);
	lv_obj_set_pos(img_view, VIEW_IMG_X, VIEW_IMG_Y);
	lv_obj_set_click(img_view, true);
	lv_obj_set_event_cb(img_view, cb_lepton_image);
	
	// Button Area
	btn_back = lv_btn_create(view_screen, NULL);
	lv_obj_set_pos(btn_back, VIEW_EXIT_BTN_X, VIEW_EXIT_BTN_Y);
	lv_obj_set_size(btn_back, VIEW_EXIT_BTN_W, VIEW_EXIT_BTN_H);
	lv_obj_set_event_cb(btn_back, cb_btn_exit);
	btn_back_label = lv_label_create(btn_back, NULL);
	lv_label_set_static_text(btn_back_label, LV_SYMBOL_CLOSE);
	
	btn_browse = lv_btn_create(view_screen, NULL);
	lv_obj_set_pos(btn_browse, VIEW_BROWSE_BTN_X, VIEW_BROWSE_BTN_Y);
	lv_obj_set_size(btn_browse, VIEW_BROWSE_BTN_W, VIEW_BROWSE_BTN_H);
	lv_obj_set_event_cb(btn_browse, cb_btn_browse);
	btn_browse_label = lv_label_create(btn_browse, NULL);
	lv_label_set_static_text(btn_browse_label, LV_SYMBOL_LIST);
	
	btn_delete = lv_btn_create(view_screen, NULL);
	lv_obj_set_pos(btn_delete, VIEW_DELETE_BTN_X, VIEW_DELETE_BTN_Y);
	lv_obj_set_size(btn_delete, VIEW_DELETE_BTN_W, VIEW_DELETE_BTN_H);
	lv_obj_set_event_cb(btn_delete, cb_btn_delete);
	btn_delete_label = lv_label_create(btn_delete, NULL);
	lv_label_set_static_text(btn_delete_label, LV_SYMBOL_TRASH);
	
	btn_range_mode = lv_btn_create(view_screen, NULL);
	lv_obj_set_pos(btn_range_mode, VIEW_RNG_MODE_BTN_X, VIEW_RNG_MODE_BTN_Y);
	lv_obj_set_size(btn_range_mode, VIEW_RNG_MODE_BTN_W, VIEW_RNG_MODE_BTN_H);
	lv_obj_set_event_cb(btn_range_mode, cb_btn_range_mode);
	btn_range_mode_label = lv_label_create(btn_range_mode, NULL);
	lv_label_set_recolor(btn_range_mode_label, true);
	
	btn_play = lv_btn_create(view_screen, NULL);
	lv_obj_set_pos(btn_play, VIEW_PLAY_BTN_X, VIEW_PLAY_BTN_Y);
	lv_obj_set_size(btn_play, VIEW_PLAY_BTN_W, VIEW_PLAY_BTN_H);
	lv_obj_set_event_cb(btn_play, cb_btn_play);
	btn_play_label = lv_label_create(btn_play, NULL);
	
	btn_prev = lv_btn_create(view_screen, NULL);
	lv_obj_set_pos(btn_prev, VIEW_PREV_BTN_X, VIEW_PREV_BTN_Y);
	lv_obj_set_size(btn_prev, VIEW_PREV_BTN_W, VIEW_PREV_BTN_H);
	lv_obj_set_event_cb(btn_prev, cb_btn_navigate);
	btn_prev_label = lv_label_create(btn_prev, NULL);
	lv_label_set_recolor(btn_prev_label, true);
	
	btn_next = lv_btn_create(view_screen, NULL);
	lv_obj_set_pos(btn_next, VIEW_NEXT_BTN_X, VIEW_NEXT_BTN_Y);
	lv_obj_set_size(btn_next, VIEW_NEXT_BTN_W, VIEW_NEXT_BTN_H);
	lv_obj_set_event_cb(btn_next, cb_btn_navigate);
	btn_next_label = lv_label_create(btn_next, NULL);
	lv_label_set_recolor(btn_next_label, true);
	
	// Spot temp (will be centered)
	lbl_spot_temp = lv_label_create(view_screen, NULL);
	lv_label_set_long_mode(lbl_spot_temp, LV_LABEL_LONG_BREAK);
	lv_label_set_align(lbl_spot_temp, LV_LABEL_ALIGN_CENTER);
	lv_obj_set_pos(lbl_spot_temp, VIEW_SPOT_TEMP_LBL_X, VIEW_SPOT_TEMP_LBL_Y);
	lv_obj_set_width(lbl_spot_temp, VIEW_SPOT_TEMP_LBL_W);
	lv_obj_set_click(lbl_spot_temp, true);
	lv_obj_set_event_cb(lbl_spot_temp, cb_spot_temp);
	
	// Modify lbl_spot_temp to use a larger font
	static lv_style_t lbl_spot_temp_style;
	lv_style_copy(&lbl_spot_temp_style, gui_st.gui_theme->style.bg);
	lbl_spot_temp_style.text.font = &lv_font_roboto_22;
	lv_label_set_style(lbl_spot_temp, LV_LABEL_STYLE_MAIN, &lbl_spot_temp_style);
	
	// Video playback info
	lbl_playback_info = lv_label_create(view_screen, NULL);
	lv_obj_set_pos(lbl_playback_info, VIEW_INFO_LBL_X, VIEW_INFO_LBL_Y);
	
	// Force immediate update of some regions
	initialize_screen_values();
	
	return view_screen;
}


/**
 * Tell this screen if it is newly active or not
 * (activating a screen (re)initializes its display)
 */
void gui_screen_view_set_active(bool en)
{
	uint16_t* ptr;
	
	if (en) {
		image_valid = false;
		
		// Request the file set by gui_screen_view_set_file_info
		if (get_file_info()) {
			request_file();
		} else {
			// Invalid file set so we'll display nothing - make sure no video controls show
			image_is_video = false;
			video_st = VIEW_PB_ST_IDLE;
			video_gui_buf_index = 1;
		}
	
		// Setup controls
		update_range_mode_btn_label();
		update_navigate_buttons();
		update_play_button();
		update_video_timestamps();
		
		// Clear the image area (to color black)
		ptr = gui_lep_canvas_buffer;
		while (ptr < (gui_lep_canvas_buffer + LEP_IMG_PIXELS)) {
			*ptr++ = 0;
		}
		
		// Draw the initial palette
		lv_canvas_fill_bg(canvas_colormap, VIEW_PALETTE_BG_COLOR);
		set_colormap(view_gui_st.palette);
		update_colormap_temps(NULL, true);
	}
}


/**
 * Set the initial absolute file index from the directory information structure
 * (catalog)
 */
void gui_screen_view_set_file_info(int abs_file_index)
{
	cur_file_index = abs_file_index;
}


/**
 * Update various playback items
 */
void gui_screen_view_set_playback_state(int st, uint32_t ts)
{
	switch (st) {
		case VIEW_PB_UPD_LEN:
			video_len_msec = ts;
			video_cur_msec = 0;
			update_video_timestamps(false);
			break;
		
		case VIEW_PB_UPD_POS:
			video_cur_msec = ts;
			update_video_timestamps();
			break;
		
		case VIEW_PB_UPD_STATE_DONE:
			video_st = VIEW_PB_ST_DONE;
			update_play_button();
			break;
	}
}


/**
 * Copy the existing main screen GUI state to our local variable as a starting point
 * for our own display state.  This should be called by another screen when an action
 * will or might end up displaying this screen.
 */
void gui_screen_view_state_init()
{
	// Copy gui_st to view_gui_st
	view_gui_st = gui_st;
	
	// Override some values for a consistent starting point
	view_gui_st.agc_enabled = false;
	view_gui_st.man_range_mode = false;
}


/**
 * Update the image just loaded by file_task
 */
void gui_screen_view_update_image()
{
	image_valid = true;
	
	// Update the file_gui_buffer[video_gui_buf_index] index to the latest updated before using it in this 
	// routine and then by functions such as colormap updates after the image is loaded
	video_gui_buf_index = (video_gui_buf_index == 0) ? 1 : 0;
	
	// Get state from the telemetry with this image
	view_gui_st.agc_enabled = (lepton_get_tel_status(file_gui_buffer[video_gui_buf_index].lep_telemP) & LEP_STATUS_AGC_STATE) == LEP_STATUS_AGC_STATE;
	view_gui_st.is_radiometric = gui_tel_is_radiometric(file_gui_buffer[video_gui_buf_index].lep_telemP);
	view_gui_st.rad_high_res = file_gui_buffer[video_gui_buf_index].lep_telemP[LEP_TEL_TLIN_RES] != 0;
	if (!image_is_video || (video_st == VIEW_PB_ST_IDLE)) {
		view_gui_st.man_range_mode = false;  // Starts off in AR for each new image or video
	}
	
	// Update image
	render_lep_data(&file_gui_buffer[video_gui_buf_index], gui_lep_canvas_buffer, &view_gui_st);
	
	// Update spot meter on image
	if (view_gui_st.spotmeter_enable && view_gui_st.is_radiometric) {
		render_spotmeter(&file_gui_buffer[video_gui_buf_index], gui_lep_canvas_buffer);
	}
	
	// Update min/max markers on image
	if (!view_gui_st.agc_enabled && view_gui_st.min_max_enable) {
		render_min_max_markers(&file_gui_buffer[video_gui_buf_index], gui_lep_canvas_buffer);
	}
	
	// Update temps
	update_colormap_temps(&file_gui_buffer[video_gui_buf_index], false);
	update_colormap_marker(&file_gui_buffer[video_gui_buf_index]);
	update_spot_temp(&file_gui_buffer[video_gui_buf_index], false);
	
	// Update Range button and filename on first image [in a video]
	if (!image_is_video || (video_st == VIEW_PB_ST_IDLE)) {
		// Configure Range button operation
		lv_obj_set_click(btn_range_mode, view_gui_st.is_radiometric);
	
		// Update Range button
		update_range_mode_btn_label();
	
		// Update filename
		update_filename(false);
	}
	
	// Finally invalidate the object to force it to redraw from the buffer
	lv_obj_invalidate(img_view);
}


/**
 * Set the btn pressed in a messagebox - used to trigger activity in response
 * to specific buttons
 */
void gui_screen_view_set_msgbox_btn(uint16_t btn)
{
	int max_file_index;
	
	max_file_index = file_get_num_files() - 1;
	
	// We're called from the message box displayed when the user clicks btn_delete
	// so request a file deletion 
	if (btn == GUI_MSG_BOX_BTN_AFFIRM) {
		// Initiate deletion of the previously set file (automatically stops any playback of the file)
		xTaskNotify(task_handle_app, APP_NOTIFY_GUI_DEL_FILE_MASK, eSetBits);
		
		// Attempt to setup the previous file to display.  If we're displaying (and attempting
		// to delete) the first image then setup the next file to display.  If there is only
		// one file then setup to exit back to the main screen.
		if (max_file_index <= 0) {
			// Only one file which is being deleted
			next_file_index = -1;
		} else if (cur_file_index <= 0) {
			// Currently displaying first image so we have to select next image_valid
			// (which will be first image after successful deletion)
			next_file_index = 0;
		} else {
			// Previous file
			next_file_index = cur_file_index - 1;
		}
	}
}


/**
 * Use the "next" set of file indexes to load the previously computed image after
 * a deletion
 */
void gui_screen_view_update_after_delete()
{
	 if (next_file_index == -1) {
	 	// Back to main screen
	 	gui_set_screen(GUI_SCREEN_MAIN);
	 } else {
	 	// Display the previously selected image
	 	cur_file_index = next_file_index;
	 	if (get_file_info()) {
			request_file();
		} else {
			// Invalid file set so we'll display nothing - make sure no video controls show
			image_is_video = false;
			video_st = VIEW_PB_ST_IDLE;
			video_gui_buf_index = 1;
		}
		update_navigate_buttons();
		update_play_button();
		update_video_timestamps();
	 }
}



//
// View Files GUI Screen internal functions
//

static void initialize_screen_values()
{
	// Initialize variables indicating no files available
	cur_file_index = 0;
	image_is_video = false;
	video_st = VIEW_PB_ST_IDLE;
	video_gui_buf_index = 1;
	
	// Force an initial update of misc on-screen items so LVGL defaults won't show
	update_filename(true);
	update_spot_temp(&file_gui_buffer[video_gui_buf_index], true);  // Force blank of text area
	update_play_button();
	update_video_timestamps();
	
	// Range button starts off disabled until we load an image so we can determine if the
	// image supported radiometric data
	lv_obj_set_click(btn_range_mode, false);
	
	// Blank the palette area
	lv_canvas_fill_bg(canvas_colormap, VIEW_PALETTE_BG_COLOR);
}


static void set_colormap(int n)
{
	set_palette(n);
	update_colormap();
}


static void set_spot_marker(int offset, bool erase)
{
	lv_point_t points[3];
	int y = offset + (VIEW_CMAP_CANVAS_HEIGHT - 256)/2;
	
	if (erase) {
		// Erase a rectangular region surrounding the drawn marker
		lv_canvas_draw_rect(canvas_colormap, VIEW_CMAP_MARKER_X1-1, y-(VIEW_CMAP_MARKER_H/2)-1,
		                    VIEW_CMAP_MARKER_X2 - VIEW_CMAP_MARKER_X1 + 2,
		                    VIEW_CMAP_MARKER_H + 2, &colormap_clear_temp_style);
	} else {
		// Draw the marker
		points[0].x = VIEW_CMAP_MARKER_X1;
		points[0].y = y;
		points[1].x = VIEW_CMAP_MARKER_X2;
		points[1].y = y-3;
		points[2].x = VIEW_CMAP_MARKER_X2;
		points[2].y = y+3;
		colormap_entry_style.body.main_color = LV_COLOR_WHITE;
		lv_canvas_draw_polygon(canvas_colormap, points, 3, &colormap_entry_style);
	}
}


static void update_colormap()
{
	lv_point_t points[2];
	int i;
	
	points[0].x = VIEW_CMAP_PALETTE_X1;
	points[1].x = VIEW_CMAP_PALETTE_X2;
	
	// Draw color map top -> bottom / hot -> cold
	for (i=0; i<256; i++) {
		points[0].y = i + (VIEW_CMAP_CANVAS_HEIGHT - 256)/2;
		points[1].y = points[0].y;
		colormap_entry_style.line.color = (lv_color_t) PALETTE_LOOKUP(255-i);
		lv_canvas_draw_line(canvas_colormap, points, 2, &colormap_entry_style);
	}
}


static void update_colormap_temps(lep_buffer_t* sys_bufP, bool init)
{
	static bool prev_agc = false;
	static bool prev_none = true;
	static bool prev_temp = false;
	static float prev_tmin = 999;
	static float prev_tmax = -999;
	
	char buf[8];
	float tmin, tmax;
	
	// Reset state when we need to force a redraw later
	if (init) {
		prev_none = true;
		return;
	}
	
	if (view_gui_st.agc_enabled) {
		// No temperature data when AGC is running
		if (!prev_agc || prev_none) {
			lv_canvas_draw_rect(canvas_colormap, 0, 2, 50, 18, &colormap_clear_temp_style);
			lv_canvas_draw_rect(canvas_colormap, 0, 280, 50, 18, &colormap_clear_temp_style);
			lv_canvas_draw_text(canvas_colormap, 5, 280, 45, &colormap_temp_style, "AGC", LV_LABEL_ALIGN_LEFT);
			prev_agc = true;
			prev_none = false;
			prev_tmin = 999;
			prev_tmax = -999;
		}
	} else if (!view_gui_st.is_radiometric) {
		// No temperature data for non-radiometric cameras
		if (prev_agc || prev_none || prev_temp) {
			lv_canvas_draw_rect(canvas_colormap, 0, 2, 50, 18, &colormap_clear_temp_style);
			lv_canvas_draw_rect(canvas_colormap, 0, 280, 50, 18, &colormap_clear_temp_style);
			lv_canvas_draw_text(canvas_colormap, 5, 280, 45, &colormap_temp_style, "RAW", LV_LABEL_ALIGN_LEFT);
			prev_agc = false;
			prev_none = false;
			prev_temp = false;
			prev_tmin = 999;
			prev_tmax = -999;
		}
	} else {
		if (view_gui_st.man_range_mode) {
			tmin = gui_range_val_to_disp_temp(view_gui_st.man_range_min);
			tmax = gui_range_val_to_disp_temp(view_gui_st.man_range_max);
		} else {
			tmin = gui_lep_to_disp_temp(sys_bufP->lep_min_val, view_gui_st.rad_high_res);
			tmax = gui_lep_to_disp_temp(sys_bufP->lep_max_val, view_gui_st.rad_high_res);
		}
		
		// High temp
		if ((tmax != prev_tmax) || prev_none) {
			gui_sprintf_temp_string(buf, tmax);
			lv_canvas_draw_rect(canvas_colormap, 0, 2, 50, 18, &colormap_clear_temp_style);
			lv_canvas_draw_text(canvas_colormap, 5, 2, 45, &colormap_temp_style, buf, LV_LABEL_ALIGN_LEFT);
			prev_tmax = tmax;
		}
	
		// Low temp
		if ((tmin != prev_tmin) || prev_none) {
			gui_sprintf_temp_string(buf, tmin);
			lv_canvas_draw_rect(canvas_colormap, 0, 280, 50, 18, &colormap_clear_temp_style);
			lv_canvas_draw_text(canvas_colormap, 5, 280, 45, &colormap_temp_style, buf, LV_LABEL_ALIGN_LEFT);
			prev_tmin = tmin;
		}
		
		prev_agc = false;
		prev_none = false;
		prev_temp = true;
	}
}


static void update_colormap_marker(lep_buffer_t* sys_bufP)
{
	static bool prev_spotmeter = false;
	static int prev_marker_offset = 0;    // Used to erase the last marker
	int marker_offset;
	uint16_t min_val, max_val;
	
	if (view_gui_st.agc_enabled || !view_gui_st.is_radiometric || !view_gui_st.spotmeter_enable) {
		// No marker when AGC is running or not a radiometric image or the spotmeter is disabled
		if (prev_spotmeter) {
			// Clear marker
			set_spot_marker(prev_marker_offset, true);
			prev_spotmeter = false;
		}
	} else {
		// Select min/max values
		if (view_gui_st.man_range_mode) {
			// Static user-set range
			if (view_gui_st.rad_high_res) {
				min_val = view_gui_st.man_range_min;
				max_val = view_gui_st.man_range_max;
			} else {
				min_val = view_gui_st.man_range_min / 10;
				max_val = view_gui_st.man_range_max / 10;
			}
		} else {
			// Dynamic range from image
			min_val = sys_bufP->lep_min_val;
			max_val = sys_bufP->lep_max_val;
		}
		
		// Update marker
		marker_offset = 255 * (max_val - sys_bufP->lep_telemP[LEP_TEL_SPOT_MEAN]) / (max_val - min_val);
		if (marker_offset < 0) marker_offset = 0;
		if (marker_offset > 255) marker_offset = 255;
		set_spot_marker(prev_marker_offset, true);
		set_spot_marker(marker_offset, false);
		prev_marker_offset = marker_offset;
		prev_spotmeter = true;
	}
}


static void update_filename(bool init)
{
	static char buf[DIR_NAME_LEN + FILE_NAME_LEN + 4];
	
	if (init) {
		buf[0] = 0;
	} else {
		sprintf(buf, "%s / %s", cur_dir_node->nameP, cur_file_node->nameP);
	}
	
	lv_label_set_static_text(lbl_view_file, buf);
}


static void update_spot_temp(lep_buffer_t* sys_bufP, bool init)
{
	static bool displayed = true;
	static char buf[8];                   // Temp for lv_label_set_static_text
	float t;
	
	if (view_gui_st.spotmeter_enable && view_gui_st.is_radiometric && !init) {
		// Update temperature display
		t = gui_lep_to_disp_temp(sys_bufP->lep_telemP[LEP_TEL_SPOT_MEAN], view_gui_st.rad_high_res);
		gui_sprintf_temp_string(buf, t);
		displayed = true;
	} else {
		if (displayed) {
			// Clear temperature display
			buf[0] = 0;
		}
		displayed = false;
	}
	
	lv_label_set_static_text(lbl_spot_temp, buf);
}


static void update_range_mode_btn_label()
{
	static char label_buf[12];  // Statically allocated, big enough for recolor symbol '#XXXXXX XX#0'
	
	// Determine brightness of label
	if (view_gui_st.agc_enabled || !view_gui_st.is_radiometric) {
		// Dimmer using a non-radiometric camera, or AGC is enabled
		strcpy(&label_buf[0], "#808080 ");
	} else {
		// Brighter when control is enabled
		strcpy(&label_buf[0], "#FFFFFF ");
	}
	
	// Update control indicating current mode
	if (view_gui_st.man_range_mode) {
		strcpy(&label_buf[8], "AR#");
	} else {
		strcpy(&label_buf[8], "MR#");
	}
	
	// Terminate and display string
	label_buf[11] = 0;
	lv_label_set_static_text(btn_range_mode_label, label_buf);
}


static void update_navigate_buttons()
{
	static char bck_buf[13];   // Statically allocated, big enough for recolor symbol + symbol '#XXXXXX sss#0'
	static char fwd_buf[13];
	int max_file_index;
	
	max_file_index = file_get_num_files() - 1;
	
	if (cur_file_index == 0) {
		// Can't go back so dim control
		sprintf(bck_buf, "#808080 " LV_SYMBOL_LEFT "#");
	} else {
		sprintf(bck_buf, "#FFFFFF " LV_SYMBOL_LEFT "#");
	}
	
	lv_label_set_static_text(btn_prev_label, bck_buf);
	
	if (cur_file_index >= max_file_index) {
		// Can't go forward so dim control
		sprintf(fwd_buf, "#808080 " LV_SYMBOL_RIGHT "#");
	} else {
		sprintf(fwd_buf, "#FFFFFF " LV_SYMBOL_RIGHT "#");
	}
	
	lv_label_set_static_text(btn_next_label, fwd_buf);
}


static void update_play_button()
{
	if (image_is_video) {
		lv_obj_set_hidden(btn_play, false);
		
		switch (video_st) {
			case VIEW_PB_ST_DONE:
			case VIEW_PB_ST_IDLE:
			case VIEW_PB_ST_PAUSE:
				lv_label_set_static_text(btn_play_label, LV_SYMBOL_PLAY);
				break;
			
			case VIEW_PB_ST_PLAY:
				lv_label_set_static_text(btn_play_label, LV_SYMBOL_PAUSE);
				break;
		}
	} else {
		// Initialize with play symbol and then hide
		lv_label_set_static_text(btn_play_label, LV_SYMBOL_PLAY);
		lv_obj_set_hidden(btn_play, true);
	}
}


static void update_video_timestamps()
{
	static char full_buf[22];     // Sized for "HHH:MM:SS / HHH:MM:SS0"
	char len_buf[10];             // Storage for "HHH:MM:SS0"
	char cur_buf[10];
	
	if (image_is_video) {
		lv_obj_set_hidden(lbl_playback_info, false);
		
		time_get_disp_string_from_msec(video_len_msec, len_buf);
		time_get_disp_string_from_msec(video_cur_msec, cur_buf);
		sprintf(full_buf, "%s / %s", cur_buf, len_buf);
		lv_label_set_static_text(lbl_playback_info, full_buf);
	} else {
		// Initialize and then hide
		lv_label_set_static_text(lbl_playback_info, "0:00 / 0:00");
		lv_obj_set_hidden(lbl_playback_info, true);
	}
	
}


static void cb_lepton_image(lv_obj_t * img, lv_event_t event)
{
	if (event == LV_EVENT_PRESSED) {		
		if (!view_gui_st.spotmeter_enable && view_gui_st.is_radiometric) {
			// Re-enable the spot display
			view_gui_st.spotmeter_enable = true;
			if (image_valid) {
				// Redraw spot information
				render_spotmeter(&file_gui_buffer[video_gui_buf_index], gui_lep_canvas_buffer);
				lv_obj_invalidate(img_view);
				update_spot_temp(&file_gui_buffer[video_gui_buf_index], false);
			}
		}
	}
}


static void cb_colormap_canvas(lv_obj_t * btn, lv_event_t event)
{
	lv_indev_t* touch;          // Input device
	lv_point_t cur_point;
	
	if (event == LV_EVENT_PRESSED) {
		touch = lv_indev_get_act();
		lv_indev_get_point(touch, &cur_point);
		if (cur_point.y < 160) {
			if (++view_gui_st.palette >= PALETTE_COUNT) view_gui_st.palette = 0;
		} else {
			if (view_gui_st.palette == 0) {
				view_gui_st.palette = PALETTE_COUNT - 1;
			} else {
				--view_gui_st.palette;
			}
		}
		
		// Update colormap display
		set_colormap(view_gui_st.palette);
		
		if (image_valid) {
			// Redraw image
			render_lep_data(&file_gui_buffer[video_gui_buf_index], gui_lep_canvas_buffer, &view_gui_st);
	
			// Update spot meter on image
			if (view_gui_st.spotmeter_enable && view_gui_st.is_radiometric) {
				render_spotmeter(&file_gui_buffer[video_gui_buf_index], gui_lep_canvas_buffer);
			}
			
			// Update min/max markers on image
			if (!view_gui_st.agc_enabled && view_gui_st.min_max_enable) {
				render_min_max_markers(&file_gui_buffer[video_gui_buf_index], gui_lep_canvas_buffer);
			}
			
			lv_obj_invalidate(img_view);
		}
	}
}


static void cb_spot_temp(lv_obj_t * btn, lv_event_t event)
{
	if (event == LV_EVENT_PRESSED) {
		if (image_valid && view_gui_st.spotmeter_enable) {
			// Disable spot display
			view_gui_st.spotmeter_enable = false;
			
			// Blank spot information (by redrawing image w/o spot)
			render_lep_data(&file_gui_buffer[video_gui_buf_index], gui_lep_canvas_buffer, &view_gui_st);
			
			// Update min/max markers on image
			if (!view_gui_st.agc_enabled && view_gui_st.min_max_enable) {
				render_min_max_markers(&file_gui_buffer[video_gui_buf_index], gui_lep_canvas_buffer);
			}
			
			lv_obj_invalidate(img_view);
			update_spot_temp(&file_gui_buffer[video_gui_buf_index], false);
		}
	}
}


static void cb_btn_exit(lv_obj_t * btn, lv_event_t event)
{
	if (event == LV_EVENT_CLICKED) {
		// Stop any ongoing video
		stop_video();
		
		// Back to the main screen
		gui_set_screen(GUI_SCREEN_MAIN);
	}
}


static void cb_btn_browse(lv_obj_t * btn, lv_event_t event)
{
	if (event == LV_EVENT_CLICKED) {
		// Stop any ongoing video
		stop_video();
		
		// Go to the file browse screen
		gui_set_screen(GUI_SCREEN_BROWSE);
	}
}


static void cb_btn_delete(lv_obj_t * btn, lv_event_t event)
{
	if (event == LV_EVENT_CLICKED) {
		// Stop the file playing if necessary
		stop_video();
		
		// Set the file to be deleted
		file_set_del_image(FILE_REQ_SRC_GUI, cur_dir_node->nameP, cur_file_node->nameP);
		
		// Make sure the user wants to delete the file - the button the user presses
		// on the messagebox will decide what we do in gui_screen_view_set_msgbox_btn
		gui_preset_message_box_string("Are you sure you want to delete the file?", true);
		xTaskNotify(task_handle_gui, GUI_NOTIFY_MESSAGEBOX_MASK, eSetBits);
	}
}


static void cb_btn_range_mode(lv_obj_t * btn, lv_event_t event)
{
	float t;
	
	if (event == LV_EVENT_CLICKED) {
		// Only do something if AGC is not enabled
		if (!view_gui_st.agc_enabled) {
			// Toggle range mode
			view_gui_st.man_range_mode = !view_gui_st.man_range_mode;
			
			// Auto-compute manual range limits if necessary
			if (view_gui_st.man_range_mode) {
				// Convert min to current display units and round down
				t = gui_lep_to_disp_temp(file_gui_buffer[video_gui_buf_index].lep_min_val, view_gui_st.rad_high_res);
				view_gui_st.man_range_min = gui_man_range_val_to_lep((uint16_t) t);
				
				// Convert max to current display units and round up
				t = gui_lep_to_disp_temp(file_gui_buffer[video_gui_buf_index].lep_max_val, view_gui_st.rad_high_res);
				view_gui_st.man_range_max = gui_man_range_val_to_lep((uint16_t) t+1);
			}
			
			// Update label
			update_range_mode_btn_label();
			
			// Update colormap info
			update_colormap_temps(&file_gui_buffer[video_gui_buf_index], false);
			update_colormap_marker(&file_gui_buffer[video_gui_buf_index]);
		}
	}
}


static void cb_btn_play(lv_obj_t * btn, lv_event_t event)
{
	if ((event == LV_EVENT_CLICKED) && image_is_video) {
		switch (video_st) {
			case VIEW_PB_ST_DONE:
				// Request file again to restart video playback process
				if (get_file_info()) {
					request_file();
				}
				// Fall through to restart
				__attribute__ ((fallthrough));
				
			case VIEW_PB_ST_IDLE:
			case VIEW_PB_ST_PAUSE:
				// Start/Unpause video playing
				xTaskNotify(task_handle_file, FILE_NOTIFY_GUI_PLAY_VIDEO_MASK, eSetBits);
				video_st = VIEW_PB_ST_PLAY;
				break;
			
			case VIEW_PB_ST_PLAY:
				// Pause video
				xTaskNotify(task_handle_file, FILE_NOTIFY_GUI_PAUSE_VIDEO_MASK, eSetBits);
				video_st = VIEW_PB_ST_PAUSE;
				break;
		}
		update_play_button();
	}
}


static void cb_btn_navigate(lv_obj_t * btn, lv_event_t event)
{
	bool update_state = false;
	
	if (event == LV_EVENT_CLICKED) {
		if (btn == btn_prev) {
			if (select_prev_file()) {
				update_state = true;
			}
		} else if (btn == btn_next) {
			if (select_next_file()) {
				update_state = true;
			}
		}
		
		if (update_state) {
			stop_video();
			if (get_file_info()) {
				request_file();
				update_play_button();
				update_video_timestamps();
			}
			update_navigate_buttons();
		}
	}
}


static bool get_file_info()
{
	int dir_index;
	int rel_file_index;
	
	if (file_get_indexes_from_abs(cur_file_index, &dir_index, &rel_file_index)) {
		cur_dir_node = file_get_indexed_directory(dir_index);
		cur_file_node = file_get_indexed_file(cur_dir_node, rel_file_index);
		image_is_video = strstr(cur_file_node->nameP, ".tmjsn") != NULL;
		video_st = VIEW_PB_ST_IDLE;
		video_gui_buf_index = 1;  // Will be flipped to first buffer when image loaded
		return true;
	} else {
		return false;
	}
}


static void request_file()
{
	file_set_get_image(FILE_REQ_SRC_GUI, cur_dir_node->nameP, cur_file_node->nameP);
	if (image_is_video) {
		xTaskNotify(task_handle_file, FILE_NOTIFY_GUI_GET_VIDEO_MASK, eSetBits);
	} else {
		xTaskNotify(task_handle_file, FILE_NOTIFY_GUI_GET_IMAGE_MASK, eSetBits);
	}
}


static bool select_next_file()
{
	int max_file_index;
	
	max_file_index = file_get_num_files() - 1;
	
	if (cur_file_index >= max_file_index) {
		// At end or past last file - catalog may have changed under us
		return false;
	} else {
		// Next file
		cur_file_index += 1;
	}
	return true;
}


static bool select_prev_file()
{
	int max_file_index;
	
	max_file_index = file_get_num_files() - 1;
	
	if ((cur_file_index == 0) || (max_file_index < 0)) {
		// At beginning of files or no files
		return false;
	} else {
		// Previous file
		cur_file_index -= 1;
	}
	return true;
}


static void stop_video()
{
	if (image_is_video && (video_st != VIEW_PB_ST_DONE)) {
		xTaskNotify(task_handle_file, FILE_NOTIFY_GUI_END_VIDEO_MASK, eSetBits);
	}
}
