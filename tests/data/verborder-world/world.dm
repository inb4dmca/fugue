// Fugue Verb Order: which verb a typed line runs when the lower verb id is learnt last, and the `<Verb> word` rows for
// several atoms of one name.
/obj/late/verb/hoot_l(n as num)
	set name = "Hoot"
	set src = view(1)
	world.log << "VERB Hoot lamp [n]"
/obj/late
	name = "lamp"
/mob/verb/hoot_m(t as text)
	set name = "Hoot"
	world.log << "VERB Hoot mob [t]"

/obj/twin
	name = "twin"
/obj/twin/verb/poke_t()
	set name = "Poke"
	set src in view()
	world.log << "VERB Poke twin [x],[y]"
/obj/twin/verb/prod_t()
	set name = "Prod"
	set src in view()
	world.log << "VERB Prod twin [x],[y]"
/obj/other
	name = "twin"
/obj/other/verb/poke_o()
	set name = "Poke"
	set src in view()
	world.log << "VERB Poke other [x],[y]"

world
	name = "Fugue Verb Order"
	mob = /mob/living/carbon/human
	view = 3
	maxx = 7
	maxy = 7
	maxz = 1
turf
	icon = null
	name = "floor"
area
	name = "room"
/mob/living/carbon/human
/mob/Login()
	if(!loc) loc = locate(4, 4, 1)
	..()
	spawn(150)
		if(!locate(/obj/late) in world)
			new /obj/late(locate(5, 4, 1))
			world.log << "SPAWNED lamp"
/world/New()
	..()
	new /obj/twin(locate(5, 5, 1))
	new /obj/other(locate(4, 6, 1))
	new /obj/twin(locate(3, 5, 1))
