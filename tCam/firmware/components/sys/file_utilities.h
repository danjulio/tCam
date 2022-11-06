/*
 * File related utilities
 *
 * Contains functions to initialize the sdmmc interface, detect and format SD Cards,
 * create directories and write image files.
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
#ifndef FILE_UTILITIES_H
#define FILE_UTILITIES_H

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>



//
// File Utilities Constants
//
#define DEF_SD_CARD_LABEL "TCAM"

// Directory and file string lengths (including null)
//
// Directory names are "tcam_YY_MM_DD"
#define DIR_NAME_LEN    16
// File names are "img_HH_MM_SS.tjsn" or "mov_HH_MM_SS.tmjsn"
#define FILE_NAME_LEN   20

// Newlib buffer size increase (see https://blog.drorgluska.com/2022/06/esp32-sd-card-optimization.html)
// Through experimentation it was discovered 8192 bytes is the largest that can be
// taken from the heap during runtime without causing memory allocation problems.
#define STREAM_BUF_SIZE 8192


//
// File System local data structure
//
typedef struct file_node_t file_node_t;

struct file_node_t {
	char* nameP;
	file_node_t* nextP;
	file_node_t* prevP;
};

typedef struct directory_node_t directory_node_t;

struct directory_node_t {
	char* nameP;
	directory_node_t* nextP;
	directory_node_t* prevP;
	file_node_t* fileP;
	int num_files;
};


//
// File Utilities API
//

// File access (file_task only)
bool file_init_sdmmc_driver();
bool file_get_card_mounted();
bool file_format_card();
bool file_init_card();
bool file_reinit_card();
bool file_mount_sdcard();
bool file_delete_directory(char* dir_name);
bool file_delete_file(char* dir_name, char* file_name);
bool file_open_image_write_file(bool is_movie, FILE** fp);
bool file_open_image_read_file(char* dir_name, char* file_name, FILE** fp);
char* file_get_open_write_dirname(bool* new);
char* file_get_open_write_filename();
int file_get_open_filelength(FILE* fp);
bool file_read_open_section(FILE* fp, char* buf, int start_pos, int len);
void file_close_file(FILE* fp);
void file_unmount_sdcard();

// Local filesystem info management (file_task only)
bool file_create_filesystem_info();
void file_delete_filesystem_info();
directory_node_t* file_add_directory_info(char* name);
file_node_t* file_add_file_info(directory_node_t* dirP, char* name);
void file_delete_directory_info(int n);
void file_delete_file_info(directory_node_t* dirP, int n);
int file_get_name_list(int type, char* list);

// Local filesystem info management (mutex protected for multiple task access)
directory_node_t* file_get_indexed_directory(int n);
int file_get_named_directory_index(char* name);
file_node_t* file_get_indexed_file(directory_node_t* dirP, int n);
int file_get_named_file_index(directory_node_t* dirP, char* name);
int file_get_num_directories();
int file_get_num_files();
int file_get_abs_file_index(int dir_index, int file_index);
bool file_get_indexes_from_abs(int abs_index, int* dir_index, int* file_index);
uint64_t file_get_storage_len();
uint64_t file_get_storage_free();


#endif /* FILE_UTILITIES_H */