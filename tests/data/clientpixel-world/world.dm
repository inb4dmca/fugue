// client.pixel_x/y/w/z and animate(client): the view offset. 21x21 map, view 5, the player at (11,11), a marker
// just past the east edge of the view at (17,11), a screen object at 1,1. Chat markers T10 .. END split the frames.
var/global/list/vals = list(8, -12, 5, 7, 32, 10, 4)

world
	fps = 10
	view = 5
	turf = /turf/floor
	mob = /mob/player
	maxx = 21
	maxy = 21
	maxz = 1

turf/floor
	icon = 'probe.dmi'
	icon_state = "floor"

mob/player
	icon = 'probe.dmi'
	icon_state = "player"

obj/marker
	icon = 'probe.dmi'
	icon_state = "marker"

obj/hud
	icon = 'probe.dmi'
	icon_state = "hud"
	screen_loc = "1,1"

world/New()
	..()
	new /obj/marker(locate(17,11,1))

mob/player/Login()
	..()
	loc = locate(11,11,1)
	client.screen += new /obj/hud
	spawn(20) client.probe()

client/proc/probe()
	src << "T10"
	pixel_x = vals[1]
	pixel_y = vals[2]
	sleep(10)
	src << "T20"
	pixel_x = 0
	pixel_y = 0
	pixel_w = vals[3]
	pixel_z = vals[4]
	sleep(10)
	src << "T30"
	pixel_w = 0
	pixel_z = 0
	animate(src, pixel_x = vals[5], time = 10)
	sleep(20)
	src << "T50"
	pixel_x = 0
	sleep(10)
	src << "T60"
	animate(src, pixel_x = vals[6], pixel_y = vals[7], time = 4, easing = SINE_EASING | EASE_OUT, flags = ANIMATION_PARALLEL | ANIMATION_RELATIVE)
	sleep(5)
	src << "T65"
	animate(src, pixel_x = 0, pixel_y = 0, time = 12, easing = SINE_EASING | EASE_IN)
	sleep(20)
	src << "END"
