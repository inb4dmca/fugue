// animate(client) timing: ANIMATION_PARALLEL overlap, kicks with a chained return, direct sets during a tween, vg's
// directional_recoil fired in a burst. A "@N" chat marker leads every tick's frames (N = world.time).
var/global/list/vals = list(32, 20, 5, 6, 8, 20, 8)

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
	spawn(0) client.ticker()
	spawn(30) client.probe()

client/proc/ticker()
	while(src)
		src << "@[world.time]"
		sleep(1)

client/proc/recoil(rx)
	animate(src, pixel_x = rx, pixel_y = 0, time = 1, easing = SINE_EASING|EASE_OUT, flags = ANIMATION_PARALLEL|ANIMATION_RELATIVE)
	sleep(2)
	animate(src, pixel_x = 0, pixel_y = 0, time = 3, easing = SINE_EASING|EASE_IN)

client/proc/probe()
	src << "P"
	animate(src, pixel_x = vals[1], time = vals[2], flags = ANIMATION_PARALLEL)
	sleep(vals[3])
	animate(src, pixel_y = vals[1], time = vals[2], flags = ANIMATION_PARALLEL)
	sleep(30)
	pixel_x = 0
	pixel_y = 0
	sleep(10)
	src << "R"
	animate(src, pixel_x = vals[6], pixel_y = vals[7], time = 4, easing = SINE_EASING|EASE_OUT, flags = ANIMATION_PARALLEL|ANIMATION_RELATIVE)
	sleep(vals[3])
	animate(src, pixel_x = 0, pixel_y = 0, time = 12, easing = SINE_EASING|EASE_IN)
	sleep(20)
	src << "C"
	animate(src, pixel_x = vals[1], time = vals[2])
	sleep(vals[3])
	pixel_x = vals[4]
	sleep(25)
	pixel_x = 0
	sleep(10)
	src << "D"
	animate(src, pixel_x = vals[1], time = vals[2])
	sleep(vals[3])
	pixel_y = vals[4]
	sleep(25)
	pixel_x = 0
	pixel_y = 0
	sleep(10)
	src << "B"
	for(var/i in 1 to vals[5])
		spawn(0) recoil(vals[1] / 2)
		sleep(2)
	sleep(30)
	src << "S"
	animate(src, pixel_x = vals[6], time = 2, easing = SINE_EASING|EASE_OUT, flags = ANIMATION_PARALLEL|ANIMATION_RELATIVE)
	sleep(vals[3])
	animate(src, pixel_x = 0, pixel_y = 0, time = 12, easing = SINE_EASING|EASE_IN)
	sleep(20)
	src << "END"
