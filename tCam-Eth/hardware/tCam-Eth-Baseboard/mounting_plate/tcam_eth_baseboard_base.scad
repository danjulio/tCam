//
// tCam-Eth baseboard base
//

// Baseboard dimensions
bb_w = 2.0 * 25.4;
bb_l = 2.75 * 25.4;
bb_mnt_offset_x = 0.1 * 25.4;
bb_mnt_offset_y = 0.1 * 25.4;
bb_mnt_delta_x = bb_w - (2 * bb_mnt_offset_x);
bb_mnt_delta_y = bb_l - (2 * bb_mnt_offset_y);

// Outer dimensions
base_w = bb_w + (0.25 * 25.4);
base_l = bb_l + (0.25 * 25.4);
base_h = 2.0;
base_mnt_x0 = ((base_w - bb_w) / 2) + bb_mnt_offset_x;
base_mnt_y0 = ((base_l - bb_l) / 2) + bb_mnt_offset_y;

// Mount post (above base plate)
base_mnt_d = 5;
base_mnt_h = 5;

// Mount post screw hole diameter (should be smaller than the screw)
base_mnt_screw_hole_d = 2.0;


module mnt_post(x, y)
{
    translate([x, y, base_h - 0.1]) {
        difference() {
            cylinder(d = base_mnt_d, h = base_mnt_h + 0.1, $fn=120);
            
            cylinder(d = base_mnt_screw_hole_d, h = base_mnt_h + 0.2, $fn=120);
        }
    }
}



union()
{
    // Base plate with some cutout in the middle to reduce print time
    difference() {
        cube([base_w, base_l, base_h]);
        
        translate([base_mnt_x0 + base_mnt_d/2, base_mnt_y0 + base_mnt_d/2, 1]) {
            cube([bb_mnt_delta_x - base_mnt_d, bb_mnt_delta_y - base_mnt_d, base_h]);
        }
    }
    
    mnt_post(base_mnt_x0, base_mnt_y0);
    mnt_post(base_mnt_x0 + bb_mnt_delta_x, base_mnt_y0);
    mnt_post(base_mnt_x0, base_mnt_y0 + bb_mnt_delta_y);
    mnt_post(base_mnt_x0 + bb_mnt_delta_x, base_mnt_y0 + bb_mnt_delta_y);
}