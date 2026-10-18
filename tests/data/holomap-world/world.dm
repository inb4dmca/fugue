// HOLOMAP: vg's station holomap and bhangmeter. The map is an image of an icon drawn at runtime (icon() + DrawBox), with an
// overlay at a pixel offset, attached to an ICONLESS screen object at "SOUTH,WEST" (vg's hud_used.holomap_obj, plane 24,
// mouse_opacity 0) and shown with client.images. A plane master is on client.screen, as vg's lighting master is.
// Icons: mkdmi.py.
world
	name = "Fugue Holomap"
	view = 4
	turf = /turf/floor
	mob = /mob/player
	fps = 10
	maxx = 10
	maxy = 10
	maxz = 1

turf/floor
	icon = 'thing.dmi'
	icon_state = "floor"

obj/screen
	plane = 24

obj/screen/master
	screen_loc = "CENTER"
	plane = 15
	appearance_flags = PLANE_MASTER
	mouse_opacity = 0

obj/screen/holomap
	screen_loc = "SOUTH,WEST"
	mouse_opacity = 0

var/drawn = "#00ff00"

mob/player
	var/image/station_map

mob/player/Login()
	..()
	loc = locate(5, 5, 1)
	client.screen += new /obj/screen/master
	var/obj/screen/holomap/H = new
	client.screen += H
	spawn(20)
		var/icon/canvas = icon('canvas.dmi', "blank")
		canvas.DrawBox(drawn, 33, 33, 64, 64)
		station_map = image(canvas)
		var/image/cursor = image('thing.dmi', "marker")
		cursor.pixel_x = 64
		cursor.pixel_y = 64
		station_map.overlays += cursor
		station_map.loc = H
		client.images += station_map
