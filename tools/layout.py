# Design (1280x480) -> panel (800x480). Shell art is cropped to x 64..1216 and squashed by SX.
X0=64; SX=800/1152
def mx(x): return round((x-X0)*SX)
# Segments: design positions (from PowerBar.qml / RpmBar.qml)
SEG_L=[(297,383),(254,361),(214,339),(175,307),(157,253),(139,200),(123,148),(109,98)]
SEG_R=[(906,383),(948,361),(991,339),(1033,307),(1070,253),(1088,200),(1106,148),(1124,98)]
L=dict(
  speed_cell0_x=70, speed_y=99, speed_pitch=88, unit_x=70+88+140, unit_y=215,
  bike_cx=400, orbit_y=176, bike_y=92,
  trip_x=564, trip_y=130,
  range_cx=400, range_y=322,
  bat_icon_x=218, bat_track_x=244, bars_y=363, track_w=140,
  temp_track_x=416, thermo_x=558,
  dock_y=410,
  tt_y=9,
)
