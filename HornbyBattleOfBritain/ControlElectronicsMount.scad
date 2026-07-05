
plate_width = 28.0;
plate_length = 28.0;
bottom_plate_height = 1.2;

controller_width = 25.0;
esp32_width = 18.0;
vreg_width = 12.0;

// 2D cross-section of the U-channel (in the X-Z plane as drawn,
// gets extruded upward along Y by linear_extrude)
module pcb_edge_profile(
    pcb_thickness  = 1.6,   // board thickness
    clearance      = 0.3,   // extra gap for fit tolerance
    wall_thickness = 3,     // thickness of the base and side walls
    wall_height    = 6      // height of the side walls above the base
) {
    inner_gap   = pcb_thickness + clearance;
    outer_width = inner_gap + 2*wall_thickness;
    outer_height= wall_height + wall_thickness;

    difference() {
        // outer U block
        square([outer_width, outer_height]);

        // inner slot: open at the top, closed at the bottom by wall_thickness
        translate([wall_thickness, wall_thickness])
            square([inner_gap, wall_height + 1]);
    }
}

// Extrude the profile along the length of the edge to be held
module pcb_edge_holder(
    pcb_thickness  = 1.6,
    clearance      = 0.3,
    wall_thickness = 3,
    wall_height    = 6,
    length         = 40     // how much of the edge the holder spans
) {
    linear_extrude(height = length)
        pcb_edge_profile(
            pcb_thickness  = pcb_thickness,
            clearance      = clearance,
            wall_thickness = wall_thickness,
            wall_height    = wall_height
        );
}

// Places a holder on each side of the board: one as-is (slot opening
// upward, +Y), one mirrored so its slot opens downward (-Y) to face
// the first. `board_width` is the distance between the two edges of
// the PCB — i.e. how far apart the two "seated" positions (where each
// edge bottoms out in its slot) should be.
//
// Note: the slot's opening direction is Y, not X — X is just the
// board-thickness gap, which is symmetric, so mirroring on X (as in
// an earlier version of this file) had no real effect. Mirroring on
// Y is what actually flips the opening to face the other holder.
module pcb_edge_holder_pair(
    pcb_thickness  = 1.6,
    clearance      = 0.3,
    wall_thickness = 3,
    wall_height    = 6,
    length         = 40,
    board_width    = 60     // distance between the two held edges
) {
    // bottom holder: base at y=0..wall_thickness, slot opens upward,
    // board's edge bottoms out at y = wall_thickness
    pcb_edge_holder(
        pcb_thickness  = pcb_thickness,
        clearance      = clearance,
        wall_thickness = wall_thickness,
        wall_height    = wall_height,
        length         = length
    );

    // top holder: mirrored on Y so its slot opens downward, then
    // shifted up so its "seated" position sits `board_width` above
    // the bottom holder's seated position
    translate([0, board_width + 2*wall_thickness, 0])
        mirror([0, 1, 0])
            pcb_edge_holder(
                pcb_thickness  = pcb_thickness,
                clearance      = clearance,
                wall_thickness = wall_thickness,
                wall_height    = wall_height,
                length         = length
            );
}

union(){
    cube([plate_width, plate_length, bottom_plate_height]);
    wall_height = 2.0;
    mount_length = 7;
    clearance = 0.2;
    
    // Motor Controller
    pcb_edge_holder_pair(board_width = controller_width+1, wall_thickness = 1, wall_height = 3, length = 12);
    
    // ESP32 Controller
    translate([10, 0, 0])
    pcb_edge_holder_pair(board_width = esp32_width+1, wall_thickness = 1, wall_height = 3, length = 12);
    
    // VREG Controller
    translate([20, 0, 0])
    pcb_edge_holder_pair(board_width = vreg_width+1, wall_thickness = 1, wall_height = 3, length = 8);
    
}

