// FLICKZ - who receives a flick() across z-levels. Three 15x15 z-levels (area/two is all of z2), view 3, the viewer at
// (8,8,1). Each step prints "F <name>" then flicks one target with its own state (8 frames x 5 ds, its own colour):
//   oz1 obj (9,8,1) in view (control) | oz2 obj (10,8,2) | oz2u obj (8,8,2) under the viewer | oz3 obj (10,9,3)
//   oz2far obj (14,14,2) | oz1far obj (14,14,1) same z, out of view | tz1 turf (9,7,1) | tz2 turf (10,7,2)
//   mz1 mob (7,9,1) | mz2 mob (7,8,2) | null obj in nullspace | az2 area/two
// Then "B": the viewer goes to (8,8,2) for 4 s and back ("B1"), and oz2, oz2u, oz3, tz2, mz2 are flicked again.
world
	name = "Fugue Flickz"
	view = 3
	turf = /turf/floor
	area = /area/one
	mob = /mob/player
	fps = 10

turf/floor
	icon = 'z.dmi'
	icon_state = "floor"
turf/ft
	icon = 'z.dmi'
	icon_state = "floor"
area
	icon = 'z.dmi'
	icon_state = "blank"
area/one
area/two
obj/o
	icon = 'z.dmi'
	icon_state = "obj"
mob/npc
	icon = 'z.dmi'
	icon_state = "mob"
mob/player
	icon = 'z.dmi'
	icon_state = "mob"

var/global/list/targets = list()
var/global/list/order = list("oz1", "oz2", "oz2u", "oz3", "oz2far", "oz1far", "tz1", "tz2", "mz1", "mz2", "null", "az2")
var/global/list/again = list("oz2", "oz2u", "oz3", "tz2", "mz2")
var/global/started = 0

world/New()
	..()
	targets["oz1"] = new /obj/o(locate(9, 8, 1))
	targets["oz2"] = new /obj/o(locate(10, 8, 2))
	targets["oz2u"] = new /obj/o(locate(8, 8, 2))
	targets["oz3"] = new /obj/o(locate(10, 9, 3))
	targets["oz2far"] = new /obj/o(locate(14, 14, 2))
	targets["oz1far"] = new /obj/o(locate(14, 14, 1))
	targets["tz1"] = new /turf/ft(locate(9, 7, 1))
	targets["tz2"] = new /turf/ft(locate(10, 7, 2))
	targets["mz1"] = new /mob/npc(locate(7, 9, 1))
	targets["mz2"] = new /mob/npc(locate(7, 8, 2))
	targets["null"] = new /obj/o
	for(var/area/two/A in world)
		targets["az2"] = A

proc/fl(name)
	world << "F [name]"
	flick("f_[name]", targets[name])

mob/player/Login()
	..()
	loc = locate(8, 8, 1)
	if(!started)
		started = 1
		spawn(0) run_scene()

mob/player/proc/run_scene()
	sleep(100)
	for(var/n in order)
		fl(n)
		sleep(60)
	world << "B"
	loc = locate(8, 8, 2)
	sleep(40)
	loc = locate(8, 8, 1)
	world << "B1"
	sleep(30)
	for(var/n in again)
		fl(n)
		sleep(60)
	world << "END"
