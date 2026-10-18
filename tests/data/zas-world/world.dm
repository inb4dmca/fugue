// ZAS - vg's gas overlays, in shape (vg code/modules/ZAS/XGM_gases.dm, Turf.dm update_graphic): a few SHARED /obj created in
// nullspace (plane 12, layer FLY_LAYER 5, mouse_opacity 0) put in TURFS' vis_contents. Player at (5,5), view 4; (4,5), (6,5)
// and (5,6) are a plain light /turf/tile, a crate stands on (6,5). Phases from Login, 5 s apart: 1 gasA on (4,5) and (6,5)
// (one obj, two turfs), gasB on (5,6); 2 (4,5) loses gasA; 3 (6,5) swaps gasA for gasB; 4 gasA added to (17,5), out of
// view; 5 the player steps to (15,5); 6 (17,5) loses gasA, (16,5) gains it, in view; 7 the player stands on (16,5).
// DreamDaemon -params "stop=N" holds phase N (for the pixel oracle). Icons: mkdmi.py.
world
	name = "Fugue Zas"
	view = 4
	turf = /turf/floor
	mob = /mob/player
	fps = 10
	maxx = 24
	maxy = 10
	maxz = 1

turf/floor
	icon = 'thing.dmi'
	icon_state = "floor"

turf/tile
	icon = 'thing.dmi'
	icon_state = "tile"

obj/crate
	icon = 'thing.dmi'
	icon_state = "crate"

obj/gas
	name = "gas"
	icon = 'thing.dmi'
	plane = 12
	layer = 5
	mouse_opacity = 0
obj/gas/a
	icon_state = "gasA"
obj/gas/b
	icon_state = "gasB"

var/global/obj/gas/a/GA
var/global/obj/gas/b/GB
var/global/phase = 0
var/global/stop_at = 99

world/New()
	..()
	if(world.params["stop"]) stop_at = text2num(world.params["stop"])
	GA = new
	GB = new
	new /turf/tile(locate(4,5,1))
	new /turf/tile(locate(6,5,1))
	new /turf/tile(locate(5,6,1))
	new /obj/crate(locate(6,5,1))

mob/player
	icon = 'thing.dmi'
	icon_state = "mob"
	Login()
		..()
		loc = locate(5, 5, 1)
		spawn(0) run_phases()

proc/T(x, y)
	return locate(x, y, 1)

mob/player/proc/run_phases()
	sleep(30)
	if(!client) return
	var/turf/t = T(4,5); t.vis_contents += GA
	t = T(6,5); t.vis_contents += GA
	t = T(5,6); t.vis_contents += GB
	phase = 1
	world << "PHASE 1"
	if(phase >= stop_at) return
	sleep(50)
	t = T(4,5); t.vis_contents -= GA
	phase = 2
	world << "PHASE 2"
	if(phase >= stop_at) return
	sleep(50)
	t = T(6,5); t.vis_contents -= GA; t.vis_contents += GB
	phase = 3
	world << "PHASE 3"
	if(phase >= stop_at) return
	sleep(50)
	t = T(17,5); t.vis_contents += GA
	phase = 4
	world << "PHASE 4"
	if(phase >= stop_at) return
	sleep(50)
	loc = T(15,5)
	phase = 5
	world << "PHASE 5"
	if(phase >= stop_at) return
	sleep(50)
	t = T(17,5); t.vis_contents -= GA
	t = T(16,5); t.vis_contents += GA
	phase = 6
	world << "PHASE 6"
	if(phase >= stop_at) return
	sleep(50)
	loc = T(16,5)
	phase = 7
	world << "PHASE 7"
