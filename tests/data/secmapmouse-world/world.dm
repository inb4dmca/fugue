// Mouse input on secondary map controls (tests/secmapmouse_test.cpp). Two builds:
//   default   Mouse procs on obj/hov and obj/hud2 only, MouseMove on obj/mov, turf/Click: which reports a hover-wanting
//             atom gets, on the default map "main.map" and on the second .dmf MAP "main.map2" (hud2 at map2:1,1 over a
//             mouse_opacity 0 background map2:1,1 to 3,3).
//   ATOM_ALL  every Mouse proc on /atom (as vg's /atom/MouseWheel flags every atom), and vg's camera console in shape:
//             3 s after login a browse() popup "campop" and, inside it, a map control created by winset
//             (cam_[ref]_map: parent=campop;type=map) showing a map_view screen object whose vis_contents are the turfs
//             (28,28)-(32,32), with hov2 on (30,30), under a mouse_opacity 0 background (code/_onclick/hud/map_popups.dm).
// 13x13 view (world.view 6) of 32 px cells centred on (20,20); hov on (22,20), mov on (18,20).
//#define ATOM_ALL

world
	name = "Fugue Secmap Input"
	fps = 10
	view = 6
	icon_size = 32
	mob = /mob/player
	turf = /turf/cell
	maxx = 40
	maxy = 40
	maxz = 1

turf/cell
	icon = 'grid.dmi'
	icon_state = "a"
obj
	icon = 'grid.dmi'
	icon_state = "hud"
obj/hov
	name = "hov"
	verb/poke()
		set src in world
		world.log << "POKE"
obj/mov
	name = "mov"
obj/hud
	name = "hud"
	icon_state = "hud2"
	screen_loc = "1,1"
obj/hud2
	name = "hud2"
	icon_state = "hud2"
	screen_loc = "map2:1,1"
obj/bg2
	name = "bg2"
	mouse_opacity = 0
	screen_loc = "map2:1,1 to 3,3"
	layer = 1
obj/map_view
	name = "screen"
	icon = null
	plane = 0
obj/background
	name = "background"
	plane = 51
	layer = 51
	mouse_opacity = 0
	alpha = 60

mob/player
	icon = null

var/global/obj/map_view/V
world/New()
	..()
	for(var/x = 1 to world.maxx)
		for(var/y = 1 to world.maxy)
			var/turf/T = locate(x, y, 1)
			if((x + y) % 2)
				T.icon_state = "b"
	new /obj/hov(locate(22, 20, 1))
	new /obj/hov(locate(30, 30, 1))
	new /obj/mov(locate(18, 20, 1))
	world.log << "WORLD READY"

mob/player/Login()
	..()
	loc = locate(20, 20, 1)
	client.screen += new /obj/hud
	client.screen += new /obj/hud2
	client.screen += new /obj/bg2
#ifdef ATOM_ALL
	spawn(30) opencam()
#endif

mob/player/proc/opencam()
	var/map = "cam_[ref(src)]_map"
	world.log << "MAP [map]"
	if(!V)
		V = new
		V.screen_loc = "[map]:1,1"
		V.vis_contents = block(locate(28,28,1), locate(32,32,1))
		var/obj/background/B = new
		B.screen_loc = "[map]:1,1 to 5,5"
		client.screen += V
		client.screen += B
	src << browse("<html><body>camera console</body></html>", "window=campop;size=420x420")
	sleep(5)
	winset(src, map, "parent=campop;type=map;pos=10,10;size=320x320")

#ifdef ATOM_ALL
atom/Click(location, control, params)
	world.log << "CLICK\t[src]\t[location]\t[control]\t[params]\t[world.time]"
atom/DblClick(location, control, params)
	world.log << "DBL\t[src]\t[location]\t[control]\t[params]\t[world.time]"
atom/MouseDown(location, control, params)
	world.log << "DOWN\t[src]\t[location]\t[control]\t[params]\t[world.time]"
atom/MouseUp(location, control, params)
	world.log << "UP\t[src]\t[location]\t[control]\t[params]\t[world.time]"
atom/MouseDrag(over_object, src_location, over_location, src_control, over_control, params)
	world.log << "DRAG\t[src]\t[over_object]\t[src_location]\t[over_location]\t[src_control]\t[over_control]\t[params]\t[world.time]"
atom/MouseDrop(over_object, src_location, over_location, src_control, over_control, params)
	world.log << "DROP\t[src]\t[over_object]\t[src_location]\t[over_location]\t[src_control]\t[over_control]\t[params]\t[world.time]"
atom/MouseWheel(delta_x, delta_y, location, control, params)
	world.log << "WHEEL\t[src]\t[location]\t[control]\t[params]\t[world.time]"
atom/MouseEntered(location, control, params)
	world.log << "ENTERED\t[src]\t[location]\t[control]\t[params]\t[world.time]"
atom/MouseExited(location, control, params)
	world.log << "EXITED\t[src]\t[location]\t[control]\t[params]\t[world.time]"
#else
obj/hov/Click(location, control, params)
	world.log << "CLICK\t[src]\t[location]\t[control]\t[params]\t[world.time]"
obj/hov/DblClick(location, control, params)
	world.log << "DBL\t[src]\t[location]\t[control]\t[params]\t[world.time]"
obj/hov/MouseDown(location, control, params)
	world.log << "DOWN\t[src]\t[location]\t[control]\t[params]\t[world.time]"
obj/hov/MouseUp(location, control, params)
	world.log << "UP\t[src]\t[location]\t[control]\t[params]\t[world.time]"
obj/hov/MouseDrag(over_object, src_location, over_location, src_control, over_control, params)
	world.log << "DRAG\t[src]\t[over_object]\t[src_location]\t[over_location]\t[src_control]\t[over_control]\t[params]\t[world.time]"
obj/hov/MouseDrop(over_object, src_location, over_location, src_control, over_control, params)
	world.log << "DROP\t[src]\t[over_object]\t[src_location]\t[over_location]\t[src_control]\t[over_control]\t[params]\t[world.time]"
obj/hov/MouseWheel(delta_x, delta_y, location, control, params)
	world.log << "WHEEL\t[src]\t[location]\t[control]\t[params]\t[world.time]"
obj/hov/MouseEntered(location, control, params)
	world.log << "ENTERED\t[src]\t[location]\t[control]\t[params]\t[world.time]"
obj/hov/MouseExited(location, control, params)
	world.log << "EXITED\t[src]\t[location]\t[control]\t[params]\t[world.time]"
obj/hud2/Click(location, control, params)
	world.log << "CLICK\t[src]\t[location]\t[control]\t[params]\t[world.time]"
obj/hud2/MouseDown(location, control, params)
	world.log << "DOWN\t[src]\t[location]\t[control]\t[params]\t[world.time]"
obj/hud2/MouseUp(location, control, params)
	world.log << "UP\t[src]\t[location]\t[control]\t[params]\t[world.time]"
obj/hud2/MouseDrag(over_object, src_location, over_location, src_control, over_control, params)
	world.log << "DRAG\t[src]\t[over_object]\t[src_location]\t[over_location]\t[src_control]\t[over_control]\t[params]\t[world.time]"
obj/hud2/MouseDrop(over_object, src_location, over_location, src_control, over_control, params)
	world.log << "DROP\t[src]\t[over_object]\t[src_location]\t[over_location]\t[src_control]\t[over_control]\t[params]\t[world.time]"
obj/hud2/MouseWheel(delta_x, delta_y, location, control, params)
	world.log << "WHEEL\t[src]\t[location]\t[control]\t[params]\t[world.time]"
obj/hud2/MouseEntered(location, control, params)
	world.log << "ENTERED\t[src]\t[location]\t[control]\t[params]\t[world.time]"
obj/hud2/MouseExited(location, control, params)
	world.log << "EXITED\t[src]\t[location]\t[control]\t[params]\t[world.time]"
turf/Click(location, control, params)
	world.log << "TCLICK\t[src]\t[params]\t[world.time]"
obj/mov/MouseMove(location, control, params)
	world.log << "MOVE\t[src]\t[params]\t[world.time]"
#endif
