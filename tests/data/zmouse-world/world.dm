// Fugue Zmouse: which turf naming (03 x y or 07 id) stock uses, per z, on the default map, a secondary map and vis_contents.
world
	name = "Fugue Zmouse"
	fps = 10
	view = 6
	icon_size = 32
	mob = /mob/player
	turf = /turf/cell
	maxx = 40
	maxy = 40
	maxz = 2

turf/cell
	icon = 'grid.dmi'
	icon_state = "a"
obj
	icon = 'grid.dmi'
	icon_state = "hud"
obj/next
	name = "next"
	icon_state = "hud2"
	screen_loc = "1,1"
obj/holder
	name = "holder"
	icon = null
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
var/global/obj/holder/H
var/global/obj/holder/H1
var/global/phase = 1
world/New()
	..()
	for(var/x = 1 to world.maxx)
		for(var/y = 1 to world.maxy)
			for(var/z = 1 to 2)
				var/turf/T = locate(x, y, z)
				if((x + y + z) % 2)
					T.icon_state = "b"
	H = new(locate(16, 20, 1))
	H.vis_contents += locate(5, 5, 2)
	H1 = new(locate(24, 20, 1))
	H1.vis_contents += locate(6, 6, 1)
	world.log << "WORLD READY"

mob/player/Login()
	..()
	loc = locate(20, 20, 1)
	client.screen += new /obj/next
	spawn(30) opencam()

obj/next/Click()
	var/mob/player/M = usr
	phase++
	world.log << "PHASE [phase]"
	if(phase == 2)
		M.loc = locate(20, 20, 2)
		V.vis_contents = block(locate(28,28,1), locate(32,32,1))
		H.loc = locate(16, 20, 2)
		H.vis_contents = list(locate(5, 5, 1))
		H1.loc = locate(24, 20, 2)
		H1.vis_contents = list(locate(6, 6, 2))
	if(phase == 3)
		M.loc = locate(20, 20, 1)
		M.client.perspective = EYE_PERSPECTIVE
		M.client.eye = locate(20, 20, 2)
		V.vis_contents = block(locate(28,28,2), locate(32,32,2))

mob/player/proc/opencam()
	var/map = "cam_[ref(src)]_map"
	world.log << "MAP [map]"
	if(!V)
		V = new
		V.screen_loc = "[map]:1,1"
		V.vis_contents = block(locate(28,28,2), locate(32,32,2))
		var/obj/background/B = new
		B.screen_loc = "[map]:1,1 to 5,5"
		client.screen += V
		client.screen += B
	src << browse("<html><body>camera console</body></html>", "window=campop;size=420x420")
	sleep(5)
	winset(src, map, "parent=campop;type=map;pos=10,10;size=320x320")

atom/Click(location, control, params)
	world.log << "CLICK\t[src]\t[x],[y],[z]\t[location]\t[control]\t[params]"
atom/DblClick(location, control, params)
	world.log << "DBL\t[src]\t[x],[y],[z]\t[location]\t[control]\t[params]"
atom/MouseDown(location, control, params)
	world.log << "DOWN\t[src]\t[x],[y],[z]\t[location]\t[control]\t[params]"
atom/MouseUp(location, control, params)
	world.log << "UP\t[src]\t[x],[y],[z]\t[location]\t[control]\t[params]"
atom/MouseDrag(over_object, src_location, over_location, src_control, over_control, params)
	var/atom/O = over_object
	world.log << "DRAG\t[src]\t[x],[y],[z]\t[O]\t[istype(O) ? "[O.x],[O.y],[O.z]" : ""]\t[src_location]\t[over_location]\t[src_control]\t[over_control]\t[params]"
atom/MouseDrop(over_object, src_location, over_location, src_control, over_control, params)
	var/atom/O = over_object
	world.log << "DROP\t[src]\t[x],[y],[z]\t[O]\t[istype(O) ? "[O.x],[O.y],[O.z]" : ""]\t[src_location]\t[over_location]\t[src_control]\t[over_control]\t[params]"
atom/MouseEntered(location, control, params)
	world.log << "ENTERED\t[src]\t[x],[y],[z]\t[location]\t[control]\t[params]"
atom/MouseExited(location, control, params)
	world.log << "EXITED\t[src]\t[x],[y],[z]\t[location]\t[control]\t[params]"
