$fn = 100;

height = 1.5;
btnRad = 1.5;
baseRad = 2;
baseHeight = 1.5;

btnSpacing = 5.5;

bevelHeight = 2 * (baseRad - btnRad);

module button() {
  cylinder(baseHeight, baseRad, baseRad);
  translate([0, 0, baseHeight+bevelHeight]) 
    cylinder(height, btnRad, btnRad);
  translate([0, 0, baseHeight]) 
    cylinder(bevelHeight, baseRad, btnRad);
  translate([0, 0, baseHeight + bevelHeight + height]) 
    scale([1, 1, .5]) 
      sphere(btnRad);
}

for (x = [0:3]) {
    translate([x*btnSpacing, 0, 0]) button();
}