// PARTICLES - BYOND particle sets, as vg uses them (code/game/objects/effects/particles_system.dm): an /obj holder whose
// `particles` is a /particles datum, on the map or in a turf's vis_contents (vg's flying_dust ZAS airflow). Player at (8,8),
// view 7, black floor. Emitters, each holder on its own tile:
//   (3,12) dots    no icon, green, 1 a tick for 20 ticks at 1.5 px a tick east, fading over the last 10
//   (3,4)  one     an 8x8 red square, still, at the centre        (6,4)  off   green, at (5,7)
//   (9,4)  half    blue at (2.5,-3.5)                              (12,4) dot1  one white dot at (0.5,0.5)
//   (3,8)  xlate   yellow, transform = matrix(1,0,6,0,1,9)        (6,8)  xlate4 cyan, a 4x4 list translating (6,9)
//   (8,12) gens    every generator kind (decode)                   (5,10) grad  a looping gradient, a 2D transform
//   (12,12) turf   a nullspace holder in the turf's vis_contents: vg's zas_dust (2 a tick, 20 max, box +-15, velocity
//                  (-3,0), colour #FFFFFF80, life 10, fade 5)
// Phases from world start, 5 s apart, unless DreamDaemon -params "hold=1": 1 the dust's velocity becomes (0,3); 2 its
// spawning 0; 3 its holder's particles = null; 4 a new /particles/dust datum. Icons: mkdmi.py.
// frames.txt: recorded against stock DreamDaemon. The pixel goldens in tests/particles_test.cpp: stock
// DreamSeeker 516.1684 under wine (hardware mode) against the same world with -params "hold=1", built with a skin whose
// map has icon-size = 32 (internal/e2e/testdata/measurevis/world.dmf) so the map is drawn 1:1; frames grabbed
// at 30 Hz for 10 s, every third one used.
world
	name = "Fugue Particles"
	view = 7
	turf = /turf/floor
	mob = /mob/player
	fps = 10
	maxx = 15
	maxy = 15
	maxz = 1

turf/floor
	icon = 'p.dmi'
	icon_state = "floor"

mob/player
	icon = 'p.dmi'
	icon_state = "blank"

obj/holder
	icon = 'p.dmi'
	icon_state = "blank"
	mouse_opacity = 0

particles/dots
	width = 200
	height = 200
	count = 100
	spawning = 1
	lifespan = 20
	fade = 10
	color = "#00FF00"
	velocity = list(1.5, 0)

particles/one
	width = 96
	height = 96
	count = 1
	spawning = 1
	lifespan = 100000
	color = "#FF0000"
	icon = 'sq.dmi'
	icon_state = "sq"

particles/one/off
	position = list(5, 7)
	color = "#00FF00"

particles/one/half
	position = list(2.5, -3.5)
	color = "#0000FF"

particles/one/xlate
	color = "#FFFF00"
	transform = matrix(1, 0, 6, 0, 1, 9)

particles/one/xlate4
	color = "#00FFFF"
	transform = list(1,0,0,0, 0,1,0,0, 0,0,1,0, 6,9,0,1)

particles/dot1
	width = 96
	height = 96
	count = 1
	spawning = 1
	lifespan = 100000
	color = "#FFFFFF"
	position = list(0.5, 0.5)

particles/gens
	width = 64
	height = 32
	count = 7
	spawning = 0.25
	bound1 = list(-50, -60, -70)
	bound2 = list(51, 61, 71)
	gravity = list(0.01, 0.02, 0.03)
	lifespan = generator("num", 5, 9)
	fade = generator("num", 1, 2, NORMAL_RAND)
	fadein = generator("num", 0.5, 1.5, LINEAR_RAND)
	color = generator("color", "#ff0000", "#0000ff")
	position = generator("circle", 3, 6)
	velocity = generator("sphere", 0.25, 0.5, SQUARE_RAND)
	drift = generator("square", 0.1, 0.2)
	friction = generator("num", 0.05, 0.1)
	scale = generator("vector", list(0.5,0.5), list(1,1))
	grow = generator("cube", 0.01, 0.02)
	rotation = generator("num", -45, 45)
	spin = generator("num", -3, 3)
	icon = list('sq.dmi' = 2, 'p.dmi' = 1)
	icon_state = list("sq" = 3, "dust" = 1)

particles/grad
	width = 64
	height = 64
	count = 9
	spawning = 0.3
	lifespan = 12
	gradient = list(0, "#ff0000", 0.5, "#00ff00", 1, "#0000ff", "loop")
	color = generator("num", 0, 1)
	color_change = 0.05
	transform = matrix(1, 0.5, 2, 0, 1, 3)
	velocity = generator("box", list(-1,-1,-1), list(1,1,1))

particles/dust
	width = 96
	height = 96
	count = 20
	spawning = 2
	color = "#FFFFFF80"
	lifespan = 10
	fade = 5
	icon = 'p.dmi'
	icon_state = "dust"
	position = generator("box", list(-15,-15), list(15,15))
	velocity = list(-3,0)

var/obj/holder/dust_holder
var/list/north = list(0, 3)

proc/emit(x, y, ptype)
	var/obj/holder/H = new(locate(x, y, 1))
	H.particles = new ptype
	return H

world/New()
	..()
	emit(3, 12, /particles/dots)
	emit(3, 4, /particles/one)
	emit(6, 4, /particles/one/off)
	emit(9, 4, /particles/one/half)
	emit(12, 4, /particles/dot1)
	emit(3, 8, /particles/one/xlate)
	emit(6, 8, /particles/one/xlate4)
	emit(8, 12, /particles/gens)
	emit(5, 10, /particles/grad)
	dust_holder = new
	dust_holder.particles = new /particles/dust
	var/turf/T = locate(12, 12, 1)
	T.vis_contents += dust_holder
	if(params["hold"]) return
	spawn(50)
		var/particles/P = dust_holder.particles
		P.velocity = north
		sleep(50)
		P.spawning = 0
		sleep(50)
		dust_holder.particles = null
		sleep(50)
		dust_holder.particles = new /particles/dust
