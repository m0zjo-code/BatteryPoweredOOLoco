// Tapered Box with bottom lip, mezzanine floor, and hole
//
// Structure from z=0 upward:
//   [0 → lip_height]                     LIP ZONE (picture-frame outer wall only)
//   [lip_height → lip_height+mezz_thick]  MEZZANINE FLOOR (solid, 3mm thick)
//   [lip_height+mezz_thick → box_height]  MAIN CAVITY (open)
//
//   Bottom width : 35 mm
//   Top width    : 31 mm
//   Length       : 86 mm  (runs along Y; front = +Y)
//   Height       : 20 mm
//   Wall         : 2  mm
//   Lip height   : 5  mm
//   Mezz thick   : 3  mm
//
//   Mezzanine hole: 3mm dia, centred on X, 27.5mm from front face (+Y side)

bottom_width = 35;
top_width    = 31;
box_length   = 86;
box_height   = 26-5;
wall         = 2.0;
lip_height   = 0.0;
mezz_thick   = 1.0;

hole_d = 3.5;
hole_y = box_length/2 - 27.5;   // 43 - 27.5 = 15.5 mm from centre toward front
hole_x = 0;

steps = 2000;
$fn   = 60;

// ---------------------------------------------------------------------------
// Arc helpers
// ---------------------------------------------------------------------------
function arc_R(hw0, hw1, H)  = -(H*H + (hw1-hw0)*(hw1-hw0)) / (2*(hw1-hw0));
function arc_wc(hw0, R)      = hw0 - R;
function arc_w(z, R, wc)     = 2 * (wc + sqrt(max(0, R*R - z*z)));

o_R  = arc_R( bottom_width/2,        top_width/2,        box_height);
o_wc = arc_wc(bottom_width/2, o_R);

i_R  = arc_R( bottom_width/2 - wall, top_width/2 - wall, box_height);
i_wc = arc_wc(bottom_width/2 - wall, i_R);

// ---------------------------------------------------------------------------
// Lofted rectangular polyhedron
// ---------------------------------------------------------------------------
module lofted_rect(w_R, w_wc, z_offset, len, h, n=200) {
    pts = [
        for (i = [0:n])
        let(z_abs = z_offset + i*h/n)
        for (c = [[-1,-1],[1,-1],[1,1],[-1,1]])
            [c[0] * arc_w(z_abs, w_R, w_wc) / 2,
             c[1] * len / 2,
             i * h / n]
    ];
    bot   = [3,2,1,0];
    tb    = n*4;
    top_f = [tb, tb+1, tb+2, tb+3];
    sides = [
        for (i = [0:n-1])
        for (s = [0:3])
        let(a=i*4+s, b=i*4+(s+1)%4, c=(i+1)*4+(s+1)%4, dd=(i+1)*4+s)
        [a,b,c,dd]
    ];
    polyhedron(points=pts, faces=concat([bot],[top_f],sides), convexity=10);
}

// ---------------------------------------------------------------------------
// Assembly
// ---------------------------------------------------------------------------
internalZOffset = lip_height + mezz_thick;
roofRecess = 5.0;


difference() {
    // Full outer solid
    lofted_rect(o_R, o_wc, 0, box_length, box_height);

    // Lip void: hollow interior from z=0 to z=lip_height (leaves outer wall ring)
    lofted_rect(i_R, i_wc, 0, box_length - 2*wall, lip_height);
    
    translate([-(top_width-wall)/2, -(box_length/2-wall), internalZOffset]) {
        cube([top_width-wall, box_length-wall*2-roofRecess, box_height-        internalZOffset],center=false);
    }
    
    coalHatchWidth = top_width - wall*2;
    translate([-coalHatchWidth/2, box_length/2 - roofRecess, internalZOffset]) {
        cube([coalHatchWidth, 10, box_height-internalZOffset],center=false);
    }

    // Mezzanine hole: 3mm dia, through full mezz floor thickness
    // 27.5mm from front face (most positive Y = +box_length/2)
    translate([hole_x, hole_y, lip_height - 0.1])
    cylinder(d=hole_d, h=mezz_thick + 0.2, $fn=60);
    
    translate([0, 50, 5])
    {
        rotate([90, 0, 0]){
            cylinder(d=3, h=20 + 0.2, $fn=60);
        }
    }
}

/*
clyDia = 110;
rotate([90, 0, 180]) {
    translate([0, -27, box_length/2 - roofRecess - wall]){
        difference() {
            cylinder(d=clyDia,h=roofRecess+wall,$fn=60);
            
            translate([0, 0, wall]){
                cylinder(d=clyDia-2,h=roofRecess+wall,$fn=60);
            }
            
            translate([0, -210, 0]){
                cube([1000, 500, 20],center=true);
            }
            
            blankingBoxLen = 50;
            translate([top_width/2 + blankingBoxLen/2, box_length/2, 0]){
                    cube([blankingBoxLen, 20, box_height*10],center=true);
                }
            rotate([0, 180, 0])
            {
                translate([top_width/2 + blankingBoxLen/2, box_length/2, 0]){
                    cube([blankingBoxLen, 20, box_height*10],center=true);
                }
            }
        }
    }
}
*/







// ---------------------------------------------------------------------------
// Info
// ---------------------------------------------------------------------------
echo("Lip void height     :", lip_height, "mm");
echo("Mezzanine floor     :", lip_height, "to", lip_height + mezz_thick, "mm");
echo("Main cavity         :", lip_height + mezz_thick, "to", box_height, "mm");
echo("Hole centre Y       :", hole_y, "mm from box centre (=", 27.5, "mm from front)");
