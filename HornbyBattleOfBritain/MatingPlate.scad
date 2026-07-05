

bottom_width = 35;
box_length   = 86;
box_height   = 5.0;

platformWidth = 32;
platformHeight = 84;

widthDelta = (bottom_width-platformWidth);
lengthDelta = (box_length-platformHeight);

floor_thickness = 2.0;
skirtHeight = 3.0;

// Measured from outer edge....
holeLocationMeas = 26.0;
holeLocation = platformHeight/2 - holeLocationMeas;
holeSize = 4;

difference() {
    cube([bottom_width, box_length, box_height],center=true);
    
    translate([0, 0, floor_thickness])
    cube([bottom_width-widthDelta, box_length-lengthDelta, box_height],center=true);
    
    translate([0, holeLocation, 0])
    cylinder(h = 10, d = holeSize, center=true, $fn=60);
    
}