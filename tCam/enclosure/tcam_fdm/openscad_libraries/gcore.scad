//
// gCore board outline with LCD
//

module gCore() {
    difference() {
        union() {
            // PCB
            cube([69, 95, 1.6]);
        
            // LCD Module
            translate([7.5, 5, 1.6]) {
                cube([56.5, 85, 4]);
            }
            
            // ESP32
            translate([0.5, 54, -0.8]) {
                cube([31.5, 18, 0.8]);
            }
            translate([7.5, 54, -3.5]) {
                cube([24.5, 18, 3.5]);
            }
            
            // Battery connector (highest item on bottom)
            translate([59, 60, -5.6]) {
                cube([7, 8, 5.6]);
            }
            
            // Qwiic connector
            translate([13, 30, -3.4]) {
                cube([4.5, 6, 3.4]);
            }
            
            // USB Connector
            translate([52, 83, -3.4]) {
                cube([10, 12, 3.4]);
            }
            
            // Charge LED
            translate([45, 93, -0.7]) {
                cube([2, 2, 0.7]);
            }
            
            // Micro-SD socket
            translate([23, 83, -1.5]) {
                cube([12, 12, 1.5]);
            }
            
            // Power button
            translate([10.5, 89, -1.9]) {
                cube([5, 6, 1.9]);
            }
            translate([11.8, 95, -1.7]) {
                cube([2.4, 1.75, 1.5]);
            }
        }
        
        // LCD cable PCB cut-out
        translate([17.5, -1, -1]) {
            cube([35, 4.5, 3.6]);
        }
        
        // Mounting holes
        translate([2.5, 2.5, -1]) {
            cylinder(h=3.6, r=1.4, $fn=120);
        }
        translate([66.5, 2.5, -1]) {
            cylinder(h=3.6, r=1.4, $fn=120);
        }
        translate([2.5, 92.5, -1]) {
            cylinder(h=3.6, r=1.4, $fn=120);
        }
        translate([66.5, 92.5, -1]) {
            cylinder(h=3.6, r=1.4, $fn=120);
        }
    }
}


gCore();