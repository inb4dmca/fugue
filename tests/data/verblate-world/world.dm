// Fugue Verb Probe: cell order under late arrival, and a 64 px icon's footprint. The player stands at 6,6.
// Drum: drum l (low ref, made first, off view) moves onto 8,8 after login, where drum h (high ref) already is.
/obj/druml/verb/drum_l()
	set name = "Drum"
	set src = view(3)
	world.log << "VERB Drum l [x],[y]"
/obj/druml
	name = "drummer"
/obj/drumh/verb/drum_h()
	set name = "Drum"
	set src = view(3)
	world.log << "VERB Drum h [x],[y]"
/obj/drumh
	name = "drummer"
// Horn: horn l (low ref) sits on 4,8; horn h (high ref, made off view) moves onto it after login.
/obj/hornl/verb/horn_l()
	set name = "Horn"
	set src = view(3)
	world.log << "VERB Horn l [x],[y]"
/obj/hornl
	name = "horner"
/obj/hornh/verb/horn_h()
	set name = "Horn"
	set src = view(3)
	world.log << "VERB Horn h [x],[y]"
/obj/hornh
	name = "horner"
// Boom: boom a (lower id) at 6,9 (3 tiles); boom b at 3,6 (3 tiles) with a 64x64 icon reaching 4,7.
/obj/booma/verb/boom_a()
	set name = "Boom"
	set src = view(4)
	world.log << "VERB Boom a [x],[y]"
/obj/booma
	name = "boomer"
/obj/boomb/verb/boom_b()
	set name = "Boom"
	set src = view(4)
	world.log << "VERB Boom b [x],[y] [bound_width]"
/obj/boomb
	name = "boomer"
	icon = 'big.dmi'

var/obj/druml/dl
var/obj/hornh/hh
world
	name = "Fugue Verb Probe"
	mob = /mob/living/carbon/human
	view = 4
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
	if(!loc) loc = locate(6, 6, 1)
	..()
	spawn(10)
		if(dl && dl.x != 8)
			dl.loc = locate(8, 8, 1)
			world.log << "DRUM l moved [dl.x],[dl.y]"
		if(hh && hh.x != 4)
			hh.loc = locate(4, 8, 1)
			world.log << "HORN h moved [hh.x],[hh.y]"
/world/New()
	..()
	dl = new /obj/druml(locate(1, 1, 1))
	new /obj/hornl(locate(4, 8, 1))
	new /obj/drumh(locate(8, 8, 1))
	hh = new /obj/hornh(locate(1, 11, 1))
	new /obj/booma(locate(6, 9, 1))
	var/obj/b = new /obj/boomb(locate(3, 6, 1))
	world.log << "boomb bw [b.bound_width] bh [b.bound_height] dl \ref[dl] hh \ref[hh]"
