// Fugue Dragout: drags released outside every map, over an OUTPUT, over a popup's non-map area, and onto a popup map
// lying over the main map.
world
	name = "Fugue Dragout"
	fps = 10
	view = 6
	icon_size = 32
	mob = /mob/player
	turf = /turf/cell
	maxx = 40
	maxy = 40

turf/cell
	icon = 'grid.dmi'
	icon_state = "a"
obj
	icon = 'grid.dmi'
	icon_state = "hud"
obj/thing
	name = "thing"
	icon_state = "hud2"
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
	new /obj/thing(locate(22, 22, 1))
	world.log << "WORLD READY"

mob/player/Login()
	..()
	loc = locate(20, 20, 1)
	spawn(30) opencam()

mob/player/proc/opencam()
	var/map = "cam_[ref(src)]_map"
	world.log << "MAP [map]"
	V = new
	V.screen_loc = "[map]:1,1"
	V.vis_contents = block(locate(30,30,1), locate(34,34,1))
	client.screen += V
	var/obj/background/B = new
	B.screen_loc = "[map]:1,1 to 5,5"
	client.screen += B
	src << browse("<html><body>camera console</body></html>", "window=campop;size=420x420")
	sleep(5)
	winset(src, map, "parent=campop;type=map;pos=10,10;size=320x320")

atom/Click(location, control, params)
	world.log << "CLICK\t[src]\t[x],[y],[z]\t[location]\t[control]\t[params]"
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
