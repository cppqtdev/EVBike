# Design (1280x480) -> panel (800x480). Shell art is cropped to design x 64..1216 and squashed by SX.
X0 = 64
SX = 800 / 1152


def mx(x):
    return round((x - X0) * SX)
