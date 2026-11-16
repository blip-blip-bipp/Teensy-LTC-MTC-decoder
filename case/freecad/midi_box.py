# Parametric box: Teensy 4.0 + Audio adaptor Rev D, 2x MAX7219 8-digit modules (stacked),
# Neutrik XLR, USB + headphone jack moved to the back (no MIDI).
# Front (y=0): displays | Back (y=D): headphone jack + USB + XLR  (all connections at the back)
# Box size is computed from the parts: width from the back-wall ports, depth from rails + board, height from the displays.
# Usage (FreeCAD Python console, or headless with freecadcmd):
#     OUT_DIR = '/path/to/output/folder'; exec(open('midi_box.py').read())
# It builds the box + lid and writes midi_box.FCStd, box_body/box_lid .step and .stl into OUT_DIR.
# Every dimension is a variable at the top of the file. The board/Teensy/display/XLR bodies are only fit-check mock-ups
# (not printed). Optional: put a 3D model of the Audio Shield at OUT_DIR/parts/audio_board/step_jbf/TeensyAudioBoard.step
# and it is used for the board mock-up; without it a plain box of the board outline is used.
import math, os
import Part
from FreeCAD import Vector as V, Placement, Rotation

# ---------------- parameters (mm) ----------------
wall, fl, lid_t = 3.0, 3.0, 3.0
corner_r = 1.5                            # radius of the four vertical outer corners (box and lid)
bottom_r = 1.0                            # radius of the box's bottom outer edge (small, so the first layers still print cleanly)
top_r = 1.2                               # radius of the lid's top outer edge (the box rim stays flat so the lid sits flush)
logo_size, logo_depth = 25.0, 0.6         # square sticker recess in the lid (size, depth)
# inner width / depth / height are computed from the parts (see "compact sizing" below)
behind_pcb_gap = 10.0                     # free space behind the display PCBs (solder pins + header cable)
lower_stop = 3.0                          # solid stop under the lower display module
min_gap = 2.5                             # minimum gap between neighbouring things on the back wall

post = 8.0                                # corner screw posts
# post_front_y (front screw posts) is computed below so the posts butt directly against the display rails
screw_pilot, screw_clear = 2.6, 3.4       # M3: pilot for the audio-board standoffs, clearance through the lid
# lid screws: M3 flat-head screw (M3x10) into a brass heat-set insert (M3 x 6 long x 5 OD) in each corner post
insert_len, insert_hole_d = 6.0, 4.6      # insert length; hole = OD 5 minus ~0.4 for the melt - TEST PRINT
insert_depth = insert_len + 1.5           # extra depth so the insert can be pushed in fully and the screw never bottoms out

# MAX7219 8-digit modules, stacked one above the other. The digit faces sit just behind the front wall
# (seen through a window); each PCB end drops down into a vertical rail (slot) and the lid holds it in.
disp_pcb_w, disp_pcb_h, disp_pcb_t = 82.3, 15.0, 1.4   # PCB: width, height, thickness - all measured
disp_total_t = 8.9                        # digit face -> back of PCB, measured (solder pins stick out further)
disp_block_w, disp_block_h = 61.0, 14.2   # both 4-digit blocks end to end - width and height measured
disp_block_dz = 0.0                       # block centre vs PCB centre (+ = up); PCB is 15.0, block 14.2, so at most +-0.4 - check
disp_ear_right = 8.5                      # bare PCB beyond the block, right end seen from the front (measured)
disp_block_dx = ((disp_pcb_w - disp_block_w - disp_ear_right) - disp_ear_right) / 2.0   # block off-centre: + = towards the right (+x)
disp_header_x = 6.2                       # PCB left end -> 5-pin header, measured (the rail must stay clear of it)
disp_face_recess = 0.3                    # digit face sits this far behind the outer front face
disp_notch_clear = 0.4                    # clearance each side of the blocks in the front notch
disp_stack_gap = 0.0                      # modules rest directly on each other in the rails (was 0.4 of dead play above them)
disp_top_gap = 0.1                        # top PCB edge -> underside of the lid (lid keeps the modules in); was 0.3
rail_overlap = 3.0                        # how far the PCB end sits inside the slot
rail_lip = 2.0                            # material in front of / behind the PCB in the rail
rail_back = 2.5                           # material beyond the PCB end
rail_clear = 0.4                          # slot clearance each side of the PCB thickness / end (0.2 was tight in carbon-fibre PETG)

# Audio adaptor Rev D (PCB 35.56 x 36.83, from PJRC drawing / STEP) + Teensy 4.0 (35.56 x 17.78)
board_x, board_y, board_t = 35.5, 36.8, 1.6
board_holes = [(3.3, 33.5), (32.3, 33.5), (19.6, 3.3)]   # mounting holes in board coords (PJRC drawing)
teensy_len, teensy_w, teensy_t = 35.56, 17.78, 1.57
teensy_cy = 20.3                          # Teensy centre line across the board (board Y)
teensy_x0 = -0.6                          # USB end overhangs the board edge by 0.6
teensy_gap = 3.0                          # audio board top -> Teensy underside (6.2 mm board-bottom to Teensy-top measured, 9.2 incl. USB)
usb_up = 1.5                              # USB centre above the Teensy PCB top
standoff_h, standoff_d = 10.0, 6.0        # audio board sits 10 mm above the floor (cable space underneath)
jack_over = 2.5                           # headphone jack sticks out of the board edge at the USB end
board_back_gap = 0.7                     # board USB-end edge -> inner back wall (jack pokes into its hole)
usb_w, usb_h = 14.0, 10.0                 # USB cut-out (room for the plug overmould)
jack_cy = 4.5                             # headphone jack centre along the board (board Y)
# jack centre height above the board underside = the middle of the Teensy PCB (measured on the real stack; the STEP jack sits lower)
jack_cz = board_t + teensy_gap + teensy_t / 2.0
jack_hole_d = 10.0                        # round hole for the 3.5 mm plug + overmould

# Neutrik / Rean RC3FDL XLR, front-mounted on the back wall (dimensions from the Neutrik DXF, halved from its 2:1 sheet)
xlr_flange_w, xlr_flange_h, xlr_flange_t = 26.0, 31.0, 2.7   # flange sits on the OUTSIDE of the wall
xlr_body_d, xlr_body_len = 23.5, 23.2                         # barrel diameter, depth behind the panel (solder cups incl.)
xlr_cut_d = 24.2                                              # panel cut-out = barrel + 0.7 print clearance
xlr_screw = [(9.5, 12.0), (-9.5, -12.0)]                      # (dx, dz) in box coords: 2 diagonal screws 19 x 24 apart.
# Seen from OUTSIDE the back wall, box +x is on the left, so this is top-left + bottom-right (as in the Neutrik DXF and the real part)
xlr_screw_d = 3.6
nut_af, nut_t, nut_clear = 5.5, 2.4, 0.6      # M3 nut: across flats, thickness, print clearance (0.2 was tight in carbon-fibre PETG)
nut_shift = (nut_clear - 0.2) / 2.0           # grow the pocket away from the XLR barrel so the thin wall on that side stays as before
nut_boss = 10.0                               # square boss on the inside of the wall around each screw
nut_boss_t = 2.8                              # boss height (nut pocket 2.6 deep leaves 0.2 + the 3 mm wall)

# floor vents
vents_on = globals().get("VENTS", False)  # floor is solid by default; set VENTS=True before exec for hex vents
hex_pitch, hex_flat = 8.0, 6.0
vent_x, vent_y = (40.0, 90.0), (18.0, 52.0)

# ---------------- derived: compact sizing ----------------
disp_face_y = disp_face_recess
pcb_front_y = disp_face_y + (disp_total_t - disp_pcb_t)    # PCB face that the digit blocks sit on
pcb_back_y = pcb_front_y + disp_pcb_t
rail_end_y = pcb_front_y + disp_pcb_t + rail_clear + rail_lip    # back face of the rail blocks (slot back face + lip)
post_front_y = rail_end_y - 0.1                                  # front posts start 0.1 mm inside the rail: one continuous solid
stack_h = 2 * disp_pcb_h + disp_stack_gap
IW = max(disp_pcb_w + 2 * rail_back + 0.4,                 # display PCB between its rails ...
         2 * post + board_y + xlr_flange_w + 4 * min_gap)  # ... or the back-wall ports, whichever is wider
IH = stack_h + disp_top_gap + lower_stop                   # display stack hangs from the lid
D = pcb_back_y + behind_pcb_gap + board_x + board_back_gap + wall   # board sits right behind the cable gap
ID = D - 2 * wall
W, H = IW + 2 * wall, fl + IH
zc = fl + IH / 2.0
# back wall, left to right: [post] board (jack + USB) | XLR [post], gaps shared equally
_L, _R = wall + post, W - wall - post
_gap = (_R - _L - board_y - xlr_flange_w) / 3.0
board_ox = _L + _gap                                       # board left edge (box X)
xlr_x = board_ox + board_y + _gap + xlr_flange_w / 2.0     # XLR centre (box X)

def box(x, y, z, dx, dy, dz): return Part.makeBox(dx, dy, dz, V(x, y, z))
def cyl(x, y, z, d, l, ax): return Part.makeCylinder(d / 2.0, l, V(x, y, z), V(*ax))
def round_top(shape, r, z):
    """Round the outer edge of the top face at height z."""
    if r <= 0: return shape
    top = [f for f in shape.Faces if abs(f.BoundBox.ZMin - z) < 1e-6 and abs(f.BoundBox.ZMax - z) < 1e-6]
    return shape.makeFillet(r, top[0].Edges)
def round_bottom(shape, r):
    """Round the outer edge of the bottom face (z = 0)."""
    if r <= 0: return shape
    bot = [f for f in shape.Faces if abs(f.BoundBox.ZMin) < 1e-6 and abs(f.BoundBox.ZMax) < 1e-6]
    return shape.makeFillet(r, bot[0].Edges)
def round_v(shape, r):
    """Round the vertical (Z-parallel) outer edges of a block."""
    if r <= 0: return shape
    edges = [e for e in shape.Edges if abs(e.Vertexes[0].Z - e.Vertexes[-1].Z) > 1e-6 and abs(e.Vertexes[0].X - e.Vertexes[-1].X) < 1e-6 and abs(e.Vertexes[0].Y - e.Vertexes[-1].Y) < 1e-6]
    return shape.makeFillet(r, edges)

# board coords -> box coords: board X runs towards the back wall (USB end), board Y runs along box X
b_origin = V(board_ox, D - wall - board_back_gap, fl + standoff_h)
b_place = Placement(b_origin, Rotation(V(0, 0, 1), -90))
def to_box(shape):
    s = shape.copy(); s.Placement = b_place.multiply(s.Placement); return s
def bpt(bx_, by_): return V(board_ox + by_, D - wall - board_back_gap - bx_, 0)

doc = App.newDocument("MidiBox")

# ---------------- body ----------------
body = round_bottom(round_v(box(0, 0, 0, W, D, H), corner_r), bottom_r).cut(box(wall, wall, fl, IW, ID, IH))
post_pos = [(wall, post_front_y), (W - wall - post, post_front_y),
            (wall, D - wall - post), (W - wall - post, D - wall - post)]
for (px, py) in post_pos:
    body = body.fuse(box(px, py, fl, post, post, IH))
cuts = []
for (px, py) in post_pos:
    cuts.append(cyl(px + post / 2, py + post / 2, H - insert_depth, insert_hole_d, insert_depth + 0.1, (0, 0, 1)))

# display windows (front wall) + PCB rails; module 1 above module 2, stack centred in the box
pcb_cy = pcb_front_y + disp_pcb_t / 2
stack_z0 = H - disp_top_gap - stack_h                      # bottom edge of the lower module (stack hangs from the lid)
disp_z = [stack_z0 + disp_pcb_h * 1.5 + disp_stack_gap,    # PCB centre heights: [upper, lower]
          stack_z0 + disp_pcb_h * 0.5]
# one notch for both rows (no rib between them), open at the top so the modules drop in; small lip at the bottom
notch_z0 = disp_z[1] + disp_block_dz - disp_block_h / 2 - 0.3
cuts.append(box(W / 2 + disp_block_dx - disp_block_w / 2 - disp_notch_clear, -0.1, notch_z0,
                disp_block_w + 2 * disp_notch_clear, wall + 0.2, H - notch_z0 + 0.1))
slot_y0, slot_y1 = pcb_cy - disp_pcb_t / 2 - rail_clear, pcb_cy + disp_pcb_t / 2 + rail_clear
for s in (-1, 1):
    xe = W / 2 + s * disp_pcb_w / 2                        # PCB end
    x0, x1 = (wall, xe + rail_overlap) if s < 0 else (xe - rail_overlap, W - wall)   # rail joins the side wall
    body = body.fuse(box(x0, wall - 0.1, fl, x1 - x0, slot_y1 + rail_lip - wall + 0.1, IH))
    g0, g1 = (xe - rail_clear, xe + rail_overlap + 0.1) if s < 0 else (xe - rail_overlap - 0.1, xe + rail_clear)
    cuts.append(box(g0, slot_y0, stack_z0, g1 - g0, slot_y1 - slot_y0, H - stack_z0 + 0.1))

# USB slot (back wall)
usb_x = bpt(0, teensy_cy).x
usb_z = fl + standoff_h + board_t + teensy_gap + teensy_t + usb_up
cuts.append(box(usb_x - usb_w / 2, D - wall - 0.1, usb_z - usb_h / 2, usb_w, wall + 0.2, usb_h))

# headphone jack hole (back wall, same end as USB)
jack_x = bpt(0, jack_cy).x
cuts.append(cyl(jack_x, D - wall - 0.1, fl + standoff_h + jack_cz, jack_hole_d, wall + 0.2, (0, 1, 0)))

# XLR (back wall, next to the board)
cuts.append(cyl(xlr_x, D - wall - 0.1, zc, xlr_cut_d, wall + 0.2, (0, 1, 0)))
nut_r = (nut_af + nut_clear) / math.sqrt(3.0)                # hex corner radius
hy = D - wall - nut_boss_t - 0.1                             # boss face (pockets and bores start here)
# keep the bosses clear of the barrel (the wall hole alone only goes through the 3 mm wall)
cuts.append(cyl(xlr_x, hy, zc, xlr_body_d + 0.4, nut_boss_t + 0.2, (0, 1, 0)))
for dx, dz in xlr_screw:
    sx, sz = xlr_x + dx, zc + dz
    bz0, bz1 = max(sz - nut_boss / 2, fl), min(sz + nut_boss / 2, H)     # keep the boss inside the box (it must not stick above the rim)
    body = body.fuse(box(sx - nut_boss / 2, D - wall - nut_boss_t, bz0, nut_boss, nut_boss_t + 0.1, bz1 - bz0))
    cuts.append(cyl(sx, hy, sz, xlr_screw_d, nut_boss_t + wall + 0.3, (0, 1, 0)))
    a0 = math.degrees(math.atan2(-dz, -dx)) - 30             # hex flat faces the barrel (thickest wall on that side)
    _d = math.hypot(dx, dz); px_, pz_ = sx + nut_shift * dx / _d, sz + nut_shift * dz / _d   # pocket centre nudged outward
    hexpts = [V(px_ + nut_r * math.cos(math.radians(a0 + 60 * i)), hy, pz_ + nut_r * math.sin(math.radians(a0 + 60 * i))) for i in range(7)]
    cuts.append(Part.Face(Part.makePolygon(hexpts)).extrude(V(0, nut_t + 0.3, 0)))   # pocket from the boss face, leaves 0.2 mm + the wall

# hex vents
r = hex_flat / math.sqrt(3.0)
row, y = 0, vent_y[0]
while vents_on and y <= vent_y[1]:
    x = vent_x[0] + (hex_pitch / 2 if row % 2 else 0)
    while x <= vent_x[1]:
        pts = [V(x + r * math.cos(math.radians(60 * i)), y + r * math.sin(math.radians(60 * i)), -0.1) for i in range(7)]
        cuts.append(Part.Face(Part.makePolygon(pts)).extrude(V(0, 0, fl + 0.2)))
        x += hex_pitch
    y += hex_pitch * 0.866
    row += 1

# standoffs under the 3 board mounting holes
for (hx, hy) in board_holes:
    p = bpt(hx, hy)
    body = body.fuse(cyl(p.x, p.y, fl - 0.1, standoff_d, standoff_h + 0.1, (0, 0, 1)))
    cuts.append(cyl(p.x, p.y, fl + 1.0, screw_pilot, standoff_h, (0, 0, 1)))

body = body.cut(Part.makeCompound(cuts)).removeSplitter()
o = doc.addObject("Part::Feature", "Body"); o.Shape = body
def style(obj, col, transp=0):          # colours only exist when FreeCAD runs with its GUI
    if getattr(obj, "ViewObject", None) is not None:
        obj.ViewObject.ShapeColor = col; obj.ViewObject.Transparency = transp
style(o, (0.35, 0.78, 0.92))

# ---------------- lid (laid out beside the body, print-ready) ----------------
lx = W + 10.0
lid = round_top(round_v(box(lx, 0, 0, W, D, lid_t), corner_r), top_r, lid_t)
lcuts = []
for (px, py) in post_pos:
    cx, cy = lx + px + post / 2, py + post / 2
    lcuts.append(cyl(cx, cy, -0.1, screw_clear, lid_t + 0.2, (0, 0, 1)))
    lcuts.append(Part.makeCone(screw_clear / 2, 3.2, 1.5, V(cx, cy, lid_t - 1.5), V(0, 0, 1)))
lcuts.append(box(lx + W / 2 - logo_size / 2, D / 2 - logo_size / 2, lid_t - logo_depth, logo_size, logo_size, logo_depth + 0.1))   # sticker recess
lid = lid.cut(Part.makeCompound(lcuts))
o = doc.addObject("Part::Feature", "Lid"); o.Shape = lid
style(o, (0.35, 0.78, 0.92))

# ---------------- mock-ups of components (fit check, not for printing) ----------------
def mock(name, shape, col):
    m = doc.addObject("Part::Feature", name); m.Shape = shape
    style(m, col, 40)
    return m

step = os.path.join(OUT_DIR, "parts", "audio_board", "step_jbf", "TeensyAudioBoard.step")
if os.path.exists(step):
    mock("Mock_AudioBoard", to_box(Part.read(step)), (0.1, 0.4, 0.2))
else:
    mock("Mock_AudioBoard", to_box(box(0, 0, 0, board_x, board_y, board_t)), (0.1, 0.4, 0.2))
tz = board_t + teensy_gap
mock("Mock_Teensy", to_box(box(teensy_x0, teensy_cy - teensy_w / 2, tz, teensy_len, teensy_w, teensy_t)), (0.1, 0.5, 0.25))
mock("Mock_USB", to_box(box(teensy_x0 - 0.6, teensy_cy - 3.75, tz + teensy_t, 5.0, 7.5, 3.0)), (0.7, 0.7, 0.7))
for i, z in enumerate(disp_z):
    pcb = box(W / 2 - disp_pcb_w / 2, pcb_front_y, z - disp_pcb_h / 2, disp_pcb_w, disp_pcb_t, disp_pcb_h)
    blk = box(W / 2 + disp_block_dx - disp_block_w / 2, disp_face_y, z + disp_block_dz - disp_block_h / 2, disp_block_w, pcb_front_y - disp_face_y, disp_block_h)
    mock("Mock_Display%d" % (i + 1), pcb.fuse(blk), (0.9, 0.9, 0.9))
mock("Mock_XLR", cyl(xlr_x, D - wall - xlr_body_len, zc, xlr_body_d, xlr_body_len + wall, (0, 1, 0))
     .fuse(box(xlr_x - xlr_flange_w / 2, D, zc - xlr_flange_h / 2, xlr_flange_w, xlr_flange_t, xlr_flange_h)), (0.6, 0.6, 0.65))

doc.recompute()
try:
    Gui.ActiveDocument.ActiveView.viewIsometric(); Gui.SendMsgToActiveView("ViewFit")
except Exception:
    pass

# ---------------- save / export ----------------
if globals().get("NO_SAVE"):
    print("built without saving"); raise SystemExit
doc.saveAs(os.path.join(OUT_DIR, "midi_box.FCStd"))
Part.export([doc.getObject("Body")], os.path.join(OUT_DIR, "box_body.step"))
Part.export([doc.getObject("Lid")], os.path.join(OUT_DIR, "box_lid.step"))
import Mesh, MeshPart
def export_stl(shape, name, move=V(0, 0, 0)):
    """Fine mesh (round holes stay round), placed at the origin of the print bed."""
    s = shape.copy(); s.translate(move)
    m = MeshPart.meshFromShape(Shape=s, LinearDeflection=0.02, AngularDeflection=0.2, Relative=False)
    m.write(os.path.join(OUT_DIR, name)); return m
export_stl(body, "box_body.stl")                       # open side up, floor on the bed
export_stl(lid, "box_lid.stl", V(-lx, 0, 0))           # lid at the origin, outer face up
print("outer %.1f x %.1f x %.1f (+%.0f lid)" % (W, D, H, lid_t), "| valid:", body.isValid(), lid.isValid())
