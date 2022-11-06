//
// Standalone enclosure for a tCam with gCore, tCam-Mini and a flat battery.
//
// Copyright 2022 (c) Dan Julio
//
// Apologies as I'm not a mechanical engineer and this is a bit of a hack...  But
// it gets the job done.
//
// Dimensions are mm.
//
// Version 1.0 - Initial release
//
// standalone_enclosure is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// standalone_enclosure is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with standalone_enclosure.  If not, see <https://www.gnu.org/licenses/>.
//
//
use <openscad_libraries/smooth_prim.scad>
use <openscad_libraries/gcore.scad>

//
// Render control
//   1 - Draw Base for STL export
//   2 - Draw IO Assembly for STL export
//   3 - Draw Bezel for STL export
//   4 - Draw Base and IO Assembly for debug
//   5 - Draw all parts for debug
render_mode = 3;

//
// Definitions
//

// Mounting screw dimensions - 4/40 x 0.25"
screw_length = 6.35;
screw_diameter = 2.8;
screw_head_diameter = 5.6;

// gCore PCB Dimensions (taken from gCore)
gcore_width = 69;
gcore_depth = 95;
gcore_height = 1.6;
gcore_lcd_width = 56.5;
gcore_lcd_depth = 85;
gcore_lcd_height = 4;
gcore_lcd_offset_x = 7.5;
gcore_lcd_offset_y = 5;
gcore_mnt_hole_d = 2.8;
gcore_mnt_hole_inset = 2.5;

// tCam-Mini hole dimensions
mini_height = 1.6;
mini_x1_from_gcore_x1 = 30.86 ;
mini_y1_from_gcore_y1 = 7.06;
mini_x_delta = 33.02;
mini_y_delta = 41.91;
mini_lep_from_gcore_x1 = 47.37;
mini_lep_from_gcore_y1 = 26.11;
mini_standoff_h = 7.4;
mini_lep_hole_d = 9;

// Base Dimensions
top_wall_thickness = 2;
wall_thickness = 2;
encl_open_space_height = 10 + 1.6 + 7 + 0.4;
encl_offset_x = 3;
encl_offset_y = 5;

encl_width = gcore_width + 2*encl_offset_x;
encl_depth = gcore_depth + 2*encl_offset_y;
encl_height = encl_open_space_height + gcore_height + wall_thickness + (gcore_lcd_height - top_wall_thickness);

// Base Mounting posts
encl_mnt_hole_d = screw_diameter - 0.25;
encl_mnt_screw_d = screw_head_diameter + 0.25;
encl_mnt_base_d = encl_mnt_screw_d + 2;
encl_mnt_inset_x = (encl_width - gcore_width)/2 + gcore_mnt_hole_inset;
encl_mnt_inset_y = (encl_depth - gcore_depth)/2 + gcore_mnt_hole_inset;
encl_mnt_x = [encl_mnt_inset_x, encl_width - encl_mnt_inset_x, encl_mnt_inset_x, encl_width - encl_mnt_inset_x];
encl_mnt_y = [encl_mnt_inset_y, encl_mnt_inset_y, encl_depth - encl_mnt_inset_y, encl_depth - encl_mnt_inset_y];
encl_mnt_base_h = wall_thickness + encl_open_space_height;
encl_mnt_screw_cutout_h = encl_mnt_base_h - (screw_length - gcore_height - (gcore_lcd_height - top_wall_thickness));

// tCam-Mini Mounting posts
mini_x1 = encl_mnt_inset_x + mini_x1_from_gcore_x1;
mini_y1 = encl_mnt_inset_y + mini_y1_from_gcore_y1;
encl_mnt_mini_x = [mini_x1, mini_x1, mini_x1 + mini_x_delta, mini_x1 + mini_x_delta];
encl_mnt_mini_y = [mini_y1, mini_y1 + mini_y_delta, mini_y1, mini_y1 + mini_y_delta];
encl_mnt_mini_h = wall_thickness + mini_standoff_h;
encl_mnt_mini_screw_cutout_h = screw_length - mini_height;
encl_mnt_mini_base_d = 7;
encl_mnt_lep_x = encl_mnt_inset_x + mini_lep_from_gcore_x1;
encl_mnt_lep_y = encl_mnt_inset_y + mini_lep_from_gcore_y1;


// tCam-Mini Lepton hole

// Base IO Assy mounting dimensions
encl_io_assy_tolerance = 0.25;
encl_io_assy_thickness = 2 + 2 * encl_io_assy_tolerance;
encl_io_assy_x_offset = 1;
encl_io_assy_y_offset = encl_depth - encl_io_assy_thickness - 1 - encl_io_assy_tolerance;
encl_io_assy_z_offset = 1;

// IO Assembly Dimensions (taken from gCore)
// This is a block extending the depth of gCore, above
// and below the PCB with cutouts for the USB connector,
// Micro-SD card and power button cap/cutout.  In addition it
// provides a very thin wall for the charge LED to light
// through.  A cutout should be made in the outside
// enclosure starting at the top of the wall to access
// this assembly.
io_assy_x_offset = encl_io_assy_x_offset + encl_io_assy_tolerance;
io_assy_y_offset = encl_io_assy_y_offset + encl_io_assy_tolerance;
io_assy_z_offset = 1;
io_assy_width = encl_width - 2 * encl_io_assy_x_offset - 2 * encl_io_assy_tolerance;
io_assy_height = encl_height - io_assy_z_offset - encl_io_assy_tolerance;
io_assy_depth = encl_io_assy_thickness - 2 * encl_io_assy_tolerance;
io_assy_btn_center_x = 13 + (encl_width - gcore_width)/2 - io_assy_x_offset;
io_assy_btn_center_y = 0;
io_assy_btn_center_z = encl_open_space_height + (wall_thickness - encl_io_assy_x_offset) - 1;
io_assy_btn_d = 5;
io_assy_btn_h = 1; // Height above surface
io_assy_btn_gap = 0.5;
io_assy_btn_hole_d = io_assy_btn_d + 2*io_assy_btn_gap;
io_assy_btn_cutout_w = 3;
io_assy_btn_cutout_l = 8;
io_assy_btn_lever_depth = io_assy_depth - 0.5;
io_assy_sd_height = 1.5;
io_assy_sd_width = 12;
io_assy_sd_cyl_d = 9;
io_assy_sd_center_x = 29 + (encl_width - gcore_width)/2 - io_assy_x_offset;
io_assy_sd_center_y = -1;
io_assy_sd_center_z = encl_open_space_height + (wall_thickness - encl_io_assy_x_offset) - 0.75;
io_assy_led_center_x = 46 + (encl_width - gcore_width)/2 - io_assy_x_offset;
io_assy_led_center_y = 0.25; // Window thickness
io_assy_led_center_z = encl_open_space_height + (wall_thickness - encl_io_assy_x_offset) - 0.25;
io_assy_led_hole_d = 3;
io_assy_usb_height = 3.4;
io_assy_usb_width = 9;
io_assy_usb_center_x = 57 + (encl_width - gcore_width)/2 - io_assy_x_offset;
io_assy_usb_center_y = -1;
io_assy_usb_center_z = encl_open_space_height + (wall_thickness - encl_io_assy_x_offset) - 1.75;
io_assy_usb_wall_thick = 1;
io_assy_usb_cutout_height = io_assy_usb_height + 3;
io_assy_usb_cutout_width = io_assy_usb_width + 2.8;

// Small button cutout on bottom
btn_cutout_width = 2.4;
btn_cutout_height = 1.6;
btn_cutout_depth = 0.75;
btn_cutout_offset_x = -(btn_cutout_width/2);
btn_cutout_offset_z = -(btn_cutout_height/2);


// Bezel Dimensions
bezel_width = encl_width;
bezel_depth = encl_depth;
bezel_height = top_wall_thickness;

bezel_cutout_tolerance = 0.25;
bezel_cutout_offset_x = encl_offset_x + gcore_lcd_offset_x - bezel_cutout_tolerance;
bezel_cutout_offset_y = encl_offset_y + gcore_lcd_offset_y - bezel_cutout_tolerance;

// Bezel LCD flex wiring cutout
bezel_flex_offset_x = encl_offset_x + 14;
bezel_flex_offset_y = encl_offset_y + gcore_lcd_offset_y - 3;
bezel_flex_offset_depth = 3;
bezel_flex_offset_width = 43;
bezel_flex_offset_height = bezel_height - 0.6;

// Bezel Mounting posts
bezel_mnt_post_d = gcore_mnt_hole_d + 2;
bezel_mnt_drill_d = screw_diameter - 0.25; //gcore_mnt_hole_d;
bezel_mnt_post_h = gcore_lcd_height - bezel_height;



//
// Modules
//
module io_assy() {
    union() {
        // Wall minus cut-outs
        difference() {
            cube([io_assy_width, io_assy_depth, io_assy_height]);
            union() {
                // Built-in Button Assembly
                //
                // lever arm cutout
                translate([io_assy_btn_center_x - io_assy_btn_cutout_l, io_assy_btn_center_y - io_assy_btn_gap, io_assy_btn_center_z - io_assy_btn_cutout_w/2]) {
                    cube([io_assy_btn_cutout_l, io_assy_depth + 2*io_assy_btn_gap, io_assy_btn_cutout_w]);
                }
                // Button hole
                translate([io_assy_btn_center_x, io_assy_btn_center_y - io_assy_btn_gap, io_assy_btn_center_z]) {
                    rotate([-90, 0, 0]) {
                        cylinder(h = io_assy_depth + 2*io_assy_btn_gap, r = io_assy_btn_hole_d/2, $fn = 120);
                    }
                }
                
                // Micro-SD card slot
                //   1. Card slot
                //   2. Finger grab cylindrical cutout
                translate([io_assy_sd_center_x - (io_assy_sd_width/2), io_assy_sd_center_y, io_assy_sd_center_z - (io_assy_sd_height/2)]) {
                    cube([io_assy_sd_width, wall_thickness + io_assy_depth + 1, io_assy_sd_height]);
                }
                translate([io_assy_sd_center_x - (io_assy_sd_width/2), io_assy_sd_center_y + io_assy_sd_cyl_d/2 + 1, io_assy_sd_center_z]) {
                    rotate([0, 90, 0]) {
                        cylinder(h = io_assy_sd_width, r = io_assy_sd_cyl_d/2, $fn=120);
                    }
                }
                
                // Charge LED
                translate([io_assy_led_center_x, io_assy_led_center_y, io_assy_led_center_z]) {
                    rotate([-90, 0, 0]) {
                        cylinder(h = io_assy_depth + 1, r = io_assy_led_hole_d/2, $fn = 120);
                    }
                }
                
                // USB Connector
                //   1. Plug slot
                //   2. Shroud slot (non-penetrating)
                translate([io_assy_usb_center_x - (io_assy_usb_width/2), io_assy_usb_center_y, io_assy_usb_center_z - (io_assy_usb_height/2)]) {
                    cube([io_assy_usb_width, wall_thickness + io_assy_depth + 1, io_assy_usb_height]);
                }
                translate([io_assy_usb_center_x - (io_assy_usb_cutout_width/2), io_assy_usb_center_y + 1 + io_assy_usb_wall_thick, io_assy_usb_center_z - (io_assy_usb_cutout_height/2)]) {
                    cube([io_assy_usb_cutout_width, wall_thickness + io_assy_depth, io_assy_usb_cutout_height]);
                }
            }
        }
    }
    // Button additional material
    difference() {
        union() {
            // Button
            translate([io_assy_btn_center_x, io_assy_btn_center_y, io_assy_btn_center_z]) {
                rotate([-90, 0, 0]) {
                    cylinder(h = io_assy_depth + io_assy_btn_h, r = io_assy_btn_hole_d/2 - io_assy_btn_gap, $fn = 120);
                }
            }
            // Lever arm
            translate([io_assy_btn_center_x - io_assy_btn_cutout_l, io_assy_btn_center_y, io_assy_btn_center_z - io_assy_btn_cutout_w/2 + io_assy_btn_gap]) {
                cube([io_assy_btn_cutout_l + io_assy_btn_gap, io_assy_btn_lever_depth, io_assy_btn_cutout_w - 2*io_assy_btn_gap]);
            }
        }
        // Subtract small cutout for PCB button tip
        translate([io_assy_btn_center_x + btn_cutout_offset_x, -1, io_assy_btn_center_z + btn_cutout_offset_z]) {
            cube([btn_cutout_width, btn_cutout_height, btn_cutout_depth + 1]);
        }
    }
}


module base(width, depth, height, wall_width) {
    union() {
        SmoothHollowCube([width, depth, height], wall_width, 0);
        translate([1.25, 1.25, 0]) {
            cube([width-2.5, depth-2.5, wall_width]);
        }
    }
}


//
// Enclosure parts
//
module standalone_base() {
    union() {
        // Base with cutout for IO Assembly
        difference() {
            union() {
                base (encl_width, encl_depth, encl_height, wall_thickness);
                for (i = [0:3]) { // Stand-offs
                    
                    // gCore
                    translate([encl_mnt_x[i], encl_mnt_y[i], 0]) {
                        cylinder(h = encl_mnt_base_h, r = encl_mnt_base_d/2, $fn = 120);
                    }
                    
                    // tCam-Mini
                    translate([encl_mnt_mini_x[i], encl_mnt_mini_y[i], 0]) {
                        cylinder(h = encl_mnt_mini_h, r = encl_mnt_mini_base_d/2, $fn = 120);
                    }
                }
            }    
            
            translate([wall_thickness, encl_io_assy_y_offset, wall_thickness]) {
                // Delete wall at IO Assembly end
                cube([encl_width - 2*wall_thickness, encl_depth - encl_io_assy_y_offset + 1, encl_height]);
            }
            translate([encl_io_assy_x_offset, encl_io_assy_y_offset, encl_io_assy_z_offset]) {
                // Cut-out for IO Assembly to fit into
                cube([io_assy_width + 2*encl_io_assy_tolerance, io_assy_depth + encl_io_assy_tolerance, io_assy_height + encl_io_assy_tolerance]);
            }
            for (i = [0:3]) {
                // gCore
                translate([encl_mnt_x[i], encl_mnt_y[i], -1]) {
                    // Stand-off screw holes
                    cylinder(h = encl_mnt_base_h + 2, r = encl_mnt_hole_d/2, $fn = 120);
                    // Stand-off screw cut-out
                    cylinder(h = encl_mnt_screw_cutout_h + 1, r = encl_mnt_screw_d/2, $fn = 120);
                }
//encl_mnt_mini_h = wall_thickness + mini_standoff_h;
//encl_mnt_mini_screw_cutout_h = screw_length - mini_height;         
                // tCam-Mini
                translate([encl_mnt_mini_x[i], encl_mnt_mini_y[i], encl_mnt_mini_h - encl_mnt_mini_screw_cutout_h]) {
                    // Stand-off screw holes
                    cylinder(h = screw_length, r = encl_mnt_hole_d/2, $fn = 120);
                }
                translate([encl_mnt_lep_x, encl_mnt_lep_y, -1]) {
                    cylinder(h = wall_thickness + 2, r = mini_lep_hole_d/2, $fn = 120);
                }
            }
        }
    }
}


module standalone_bezel() {
    // Stand-offs below bezel
    for (i = [0:3]) {
        difference() {
            translate([encl_mnt_x[i], encl_mnt_y[i], 0]) {
                cylinder(h = bezel_mnt_post_h + 1, r = bezel_mnt_post_d/2, $fn = 120);
            }
            translate([encl_mnt_x[i], encl_mnt_y[i], -1]) {
                cylinder(h = bezel_mnt_post_h + 2, r = bezel_mnt_drill_d/2, $fn = 120);
            }
        }
    }
    
    // Bezel with cutouts
    difference() {
        // Bezel surface
        translate([0, 0, bezel_mnt_post_h]) {
                SmoothCube([bezel_width, bezel_depth, bezel_height], 1);
        }
        
        // LCD Cutout
        translate([bezel_cutout_offset_x, bezel_cutout_offset_y, bezel_mnt_post_h - 1]) {
            cube([gcore_lcd_width + 2*bezel_cutout_tolerance, gcore_lcd_depth + 2*bezel_cutout_tolerance, bezel_height + 2]);
        }
        
        // LCD flex cable cutout
        translate([bezel_flex_offset_x, bezel_flex_offset_y, bezel_mnt_post_h - 1]) {
            cube([bezel_flex_offset_width, bezel_flex_offset_depth, bezel_flex_offset_height + 1]);
        }
    }
}



//
// Render code
//
if (render_mode == 1) {
    standalone_base();
}

if (render_mode == 2) {
    rotate([90, 0, 0]) {
        io_assy();
    }
}

if (render_mode == 3) {
    rotate([-180, 0, 0]) {
        standalone_bezel();
    }
}

if (render_mode == 4) {
    #standalone_base();
    
    translate([io_assy_x_offset, io_assy_y_offset, io_assy_z_offset]) {
        io_assy();
    }
}

if (render_mode == 5) {
    #standalone_base();
    
    translate([io_assy_x_offset, io_assy_y_offset, io_assy_z_offset]) {
        #io_assy();
    }
    
    translate([(encl_width - gcore_width)/2, (encl_depth - gcore_depth)/2, encl_mnt_base_h]) {
        gCore();
    }
    
    translate([0, 0, encl_height-bezel_mnt_post_h]) {
        #standalone_bezel();
    }
}
    
