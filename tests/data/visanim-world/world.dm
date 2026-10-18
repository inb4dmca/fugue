// visanim - how stock DreamSeeker 516 animates and stacks vis_contents items (tests/visanim_test.cpp). Shared nullspace
// /obj on plane 12 go in TURFS' vis_contents; player at (5,5), view 4. Phases from Login, 6 s apart (IND at (1,1) shows
// the phase): 1 C1 (icon_state "once", a 3-frame loop=1 state, set at world start) into (4,5), C2 ("plain") into (6,5),
// C3 ("once") into (5,6), CY (looping "cyc") into (7,3) beside a map obj with the same state at (3,3), OA then OB into
// (8,5) and OB then OA into (8,6); 2 C2 becomes "once"; 3 (5,6) changes its own icon_state, C1 also goes into (4,7);
// 4 C3 also goes into (6,7). Icons: mkdmi.py.
world
	name = "Vis Probe"
	view = 4
	turf = /turf/floor
	mob = /mob/player
	fps = 10
	maxx = 12
	maxy = 10
	maxz = 1
turf/floor
	icon = 'thing.dmi'
	icon_state = "floor"
turf/tile
	icon = 'thing.dmi'
	icon_state = "tile"
obj/thing
	icon = 'thing.dmi'
obj/kid
	icon = 'thing.dmi'
	plane = 12
	layer = 5
	mouse_opacity = 0
var/global/obj/kid/C1
var/global/obj/kid/C2
var/global/obj/kid/C3
var/global/obj/kid/CY
var/global/obj/kid/OA
var/global/obj/kid/OB
var/global/obj/thing/IND
var/global/list/states = list("once", "plain", "cyc", "ordA", "ordB", "p1", "p2", "p3", "p4", "tile2")
proc/T(x, y)
	return locate(x, y, 1)
world/New()
	..()
	C1 = new; C1.icon_state = states[1]
	C2 = new; C2.icon_state = states[2]
	C3 = new; C3.icon_state = states[1]
	CY = new; CY.icon_state = states[3]
	OA = new; OA.icon_state = states[4]
	OB = new; OB.icon_state = states[5]
	for(var/xy in list(list(4,5), list(6,5), list(5,6), list(4,7), list(6,7), list(8,5), list(8,6), list(3,3), list(7,3)))
		new /turf/tile(T(xy[1], xy[2]))
	IND = new /obj/thing(T(1,1)); IND.icon_state = "p0"
	var/obj/thing/m = new(T(9,9)); m.icon_state = "mark"
	var/obj/thing/ctl = new(T(3,3)); ctl.icon_state = states[3]
mob/player
	icon = 'thing.dmi'
	icon_state = "mob"
	Login()
		..()
		loc = T(5, 5)
		spawn(0) run_phases()
mob/player/proc/run_phases()
	sleep(32)
	if(!client) return
	var/turf/t
	t = T(4,5); t.vis_contents += C1
	t = T(6,5); t.vis_contents += C2
	t = T(5,6); t.vis_contents += C3
	t = T(7,3); t.vis_contents += CY
	t = T(8,5); t.vis_contents += OA; t.vis_contents += OB
	t = T(8,6); t.vis_contents += OB; t.vis_contents += OA
	IND.icon_state = states[6]
	world << "PHASE 1"
	sleep(60)
	C2.icon_state = states[1]
	IND.icon_state = states[7]
	world << "PHASE 2"
	sleep(60)
	t = T(5,6); t.icon_state = states[10]
	t = T(4,7); t.vis_contents += C1
	IND.icon_state = states[8]
	world << "PHASE 3"
	sleep(60)
	t = T(6,7); t.vis_contents += C3
	C1.pixel_x = 0 + length(states) - 10
	IND.icon_state = states[9]
	world << "PHASE 4"
