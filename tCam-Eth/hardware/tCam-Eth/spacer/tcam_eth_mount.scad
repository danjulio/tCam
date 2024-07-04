//
// tCam-Eth mounting bracket
//

// Outer dimensions
mnt_w = 1.25 * 25.4;
mnt_l = 1.5 * 25.4;
mnt_h = 2.5;

// Wall width
wall_w = 4;

// Mounting hole locations
hole_center_x = 0.1 * 25.4;
hole_center_y = 0.1 * 25.4;
hole_d = 0.126 * 25.4;

difference() {
    union() {
        difference() {
            cube([mnt_w, mnt_l, mnt_h]);
            
            translate([wall_w, -0.1, -0.1]) {
                cube([mnt_w - 2*wall_w, mnt_l - wall_w, mnt_h + 0.2]);
            }
        }
        
        translate([hole_center_x, hole_center_y, 0]) {
            cylinder(d = hole_center_x * 2, h = mnt_h, $fn=120);
        }

        translate([mnt_w - hole_center_x, hole_center_y, 0]) {
            cylinder(d = hole_center_x * 2, h = mnt_h, $fn=120);
        }
        
        translate([hole_center_x, mnt_l - hole_center_y, 0]) {
            cylinder(d = hole_center_x * 2, h = mnt_h, $fn=120);
        }
        
        translate([mnt_w - hole_center_x, mnt_l - hole_center_y, 0]) {
            cylinder(d = hole_center_x * 2, h = mnt_h, $fn=120);
        }
    }
    
    translate([hole_center_x, hole_center_y, -0.1]) {
        cylinder(d = hole_d, h = mnt_h + 0.2, $fn=120);
    }
    
    translate([mnt_w - hole_center_x, hole_center_y, -0.1]) {
        cylinder(d = hole_d, h = mnt_h + 0.2, $fn=120);
    }
    
    translate([hole_center_x, mnt_l - hole_center_y, -0.1]) {
        cylinder(d = hole_d, h = mnt_h + 0.2, $fn=120);
    }
    
    translate([mnt_w - hole_center_x, mnt_l - hole_center_y, -0.1]) {
        cylinder(d = hole_d, h = mnt_h + 0.2, $fn=120);
    }
}