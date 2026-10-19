// Fugue Verb Wide: the player at 6,6 has bounds reaching two tiles west (bound_x -64, bound_width 96).
// Yelp: yelp a (lower id) 2 tiles east of the player's cell, yelp b 3 tiles west of it (1 past its footprint).
// DreamDaemon centres view() on the far side (view(5) of this mob spans x 3..13), so nothing sits further west.
/obj/yelpa/verb/yelp_a()
	set name = "Yelp"
	set src = view(5)
	world.log << "VERB Yelp a [x],[y]"
/obj/yelpa
	name = "yelper"
/obj/yelpb/verb/yelp_b()
	set name = "Yelp"
	set src = view(5)
	world.log << "VERB Yelp b [x],[y]"
/obj/yelpb
	name = "yelper"
// Aim: an obj argument; pad 1 (lower ref) 2 east, pad 2 3 west.
/obj/pad
	name = "pad"
/mob/verb/aim(obj/o as obj in view(5))
	world.log << "VERB Aim [o.x],[o.y]"

world
	name = "Fugue Verb Wide"
	mob = /mob/living/carbon/human
	view = 5
	maxx = 11
	maxy = 11
	maxz = 1
turf
	icon = null
	name = "floor"
area
	name = "room"
/mob/living/carbon/human
/mob/Login()
	bound_x = -64
	bound_width = 96
	if(!loc) loc = locate(6, 6, 1)
	..()
	world.log << "LOGIN [x],[y] [bound_x] [bound_width]"
/world/New()
	..()
	new /obj/yelpa(locate(8, 6, 1))
	new /obj/yelpb(locate(3, 6, 1))
	new /obj/pad(locate(8, 7, 1))
	new /obj/pad(locate(3, 7, 1))
