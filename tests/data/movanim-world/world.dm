// animate() chains on objs, mobs and an image: sequential steps, ANIMATION_PARALLEL, a later animate() taking over
// (a head), delay, loop, ANIMATION_END_NOW, colour/alpha/transform, and vg's attack-animation and rapid re-animate
// shapes. The suite runs every 100 ticks from offset 10; a "@N" chat marker leads every tick's frames (N = world.time),
// and is repeated just before each later animate() so its frames cannot be read as the tick before.
var/global/list/v = list(32, 10, 5, 20, 16, 2, 8, 3, 0, 4)
var/global/blue = "#0000ff"
var/global/list/slots = list()
var/global/image/img

world
	name = "Fugue Movable Anim"
	fps = 10
	view = 6
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

mob/npc
	icon = 'probe.dmi'

obj/slot
	icon = 'probe.dmi'

world/New()
	..()
	var/list/xs = list(5, 8, 13, 16)
	var/list/ys = list(16, 13, 8, 5)
	for(var/i in 0 to 15)
		var/atom/movable/a
		var/turf/T = locate(xs[i % 4 + 1], ys[round(i / 4) + 1], 1)
		if(i == 8 || i == 9)
			a = new /mob/npc(T)
		else
			a = new /obj/slot(T)
		a.icon_state = (i == 6) ? "white" : "c[i]"
		slots += a

mob/player/Login()
	..()
	loc = locate(11,11,1)
	client.fps = 40
	img = image('probe.dmi', slots[13], "img")
	client.images += img
	spawn(0) client.ticker()
	spawn(0) suite()

proc/mark()
	for(var/client/C)
		C << "@[world.time]"

client/proc/ticker()
	while(src)
		src << "@[world.time]"
		sleep(1)

proc/suite()
	while(1)
		for(var/atom/movable/a in slots)
			animate(a)
			a.pixel_x = 0
			a.pixel_y = 0
			a.transform = null
			a.color = null
			a.alpha = 255
		animate(img)
		img.pixel_x = 0
		img.pixel_y = 0
		sleep(10)
		var/atom/movable/s = slots[16]
		s.icon_state = "sync"
		spawn(0) seq(slots[1])
		spawn(0) par(slots[2])
		spawn(0) head(slots[3])
		spawn(0) delayed(slots[4])
		spawn(0) looped(slots[5])
		spawn(0) endnow(slots[6])
		spawn(0) coloralpha(slots[7])
		spawn(0) xformpar(slots[8])
		spawn(0) rapid(slots[9])
		spawn(0) attack(slots[10])
		spawn(0) kicks(slots[11])
		spawn(0) forever(slots[12])
		spawn(0) imagepar()
		spawn(0) sametick(slots[14])
		spawn(0) loophead(slots[15])
		sleep(50)
		s.icon_state = "c15"
		sleep(40)

proc/seq(atom/movable/a)
	animate(a, pixel_x = v[1], time = v[2])
	animate(pixel_y = v[1], time = v[2])

proc/par(atom/movable/a)
	animate(a, pixel_x = v[1], time = v[4])
	sleep(v[3])
	mark()
	animate(a, pixel_y = v[1], time = v[4], flags = ANIMATION_PARALLEL)

proc/head(atom/movable/a)
	animate(a, pixel_x = v[1], time = v[4])
	sleep(v[3])
	mark()
	animate(a, pixel_x = v[9], time = v[2])

proc/delayed(atom/movable/a)
	animate(a, pixel_x = v[1], time = v[2], delay = v[3])
	animate(pixel_y = v[1], time = v[2], delay = v[3])

proc/looped(atom/movable/a)
	animate(a, pixel_x = v[5], time = v[3], loop = v[8])
	animate(pixel_x = v[9], time = v[3])

proc/endnow(atom/movable/a)
	animate(a, pixel_x = v[1], time = v[4])
	sleep(v[3])
	mark()
	animate(a, pixel_y = v[1], time = v[2], flags = ANIMATION_END_NOW)

proc/coloralpha(atom/movable/a)
	animate(a, color = blue, time = v[2])
	sleep(v[3])
	mark()
	animate(a, alpha = v[9], time = v[2], flags = ANIMATION_PARALLEL)

proc/xformpar(atom/movable/a)
	animate(a, transform = matrix(v[6], 0, 0, 0, v[6], 0), time = v[2])
	sleep(v[3])
	mark()
	animate(a, pixel_x = v[1], time = v[2], flags = ANIMATION_PARALLEL)

proc/rapid(atom/movable/a)
	for(var/i in 1 to v[10])
		mark()
		animate(a, pixel_y = (i % 2) * v[7], time = v[8])
		sleep(v[6])

proc/attack(atom/movable/a)
	animate(a, pixel_x = a.pixel_x + v[7], time = v[6])
	animate(pixel_x = v[9], time = v[6])

proc/kicks(atom/movable/a)
	for(var/i in 1 to v[10])
		mark()
		spawn(0) kick(a)
		sleep(v[6])

proc/kick(atom/movable/a)
	animate(a, pixel_x = v[7], time = 1, flags = ANIMATION_PARALLEL|ANIMATION_RELATIVE)
	sleep(v[6])
	mark()
	animate(a, pixel_x = v[9], time = v[8])

proc/forever(atom/movable/a)
	animate(a, pixel_y = v[5], time = v[3], loop = -1)
	animate(pixel_y = v[9], time = v[3])
	sleep(v[4] + v[3])
	mark()
	animate(a)

proc/imagepar()
	animate(img, pixel_x = v[1], time = v[2])
	sleep(v[3])
	mark()
	animate(img, pixel_y = v[1], time = v[2], flags = ANIMATION_PARALLEL)

proc/sametick(atom/movable/a)
	animate(a, pixel_x = v[1], time = v[2])
	animate(a, pixel_y = v[1], time = v[2])

proc/loophead(atom/movable/a)
	animate(a, pixel_x = v[5], time = v[3], loop = -1)
	animate(pixel_x = v[9], time = v[3])
	sleep(v[3] + v[6])
	mark()
	animate(a, pixel_y = v[5], time = v[3])
