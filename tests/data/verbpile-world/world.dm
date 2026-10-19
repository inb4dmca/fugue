// Fugue Verb Pile: a cell of 52 objs (51 or more takes the client's other ordering branch). The player stands at 7,7.
// Hoot 10,10: hoot a, then 50 plain objs, at boot; hoot b (the lowest ref, parked out of sight) arrives at T+1.
// Toot 4,4: the same with 47 plain objs (49 in all), the small branch.
/obj/hoota/verb/hoot_a()
	set name = "Hoot"
	set src = view(5)
	world.log << "VERB Hoot a [x],[y] \ref[src]"
/obj/hoota
	name = "hooter"
/obj/hootb/verb/hoot_b()
	set name = "Hoot"
	set src = view(5)
	world.log << "VERB Hoot b [x],[y] \ref[src]"
/obj/hootb
	name = "hooter"
/obj/toota/verb/toot_a()
	set name = "Toot"
	set src = view(5)
	world.log << "VERB Toot a [x],[y] \ref[src]"
/obj/toota
	name = "tooter"
/obj/tootb/verb/toot_b()
	set name = "Toot"
	set src = view(5)
	world.log << "VERB Toot b [x],[y] \ref[src]"
/obj/tootb
	name = "tooter"
/obj/filler
	name = "filler"
var/obj/hb
var/obj/tb
var/started = 0
world
	name = "Fugue Verb Pile"
	mob = /mob/living/carbon/human
	view = 5
	maxx = 20
	maxy = 20
	maxz = 1
turf
	icon = null
	name = "floor"
area
	name = "room"
/mob/living/carbon/human
/mob/living/carbon/human/Login()
	if(!loc) loc = locate(7, 7, 1)
	..()
	if(started) return
	started = 1
	spawn(10)
		hb.loc = locate(10, 10, 1)
		tb.loc = locate(4, 4, 1)
		world.log << "T1 hb \ref[hb] [hb.x],[hb.y] tb \ref[tb] [tb.x],[tb.y]"
/world/New()
	..()
	hb = new /obj/hootb(locate(18, 1, 1))
	tb = new /obj/tootb(locate(18, 2, 1))
	var/obj/ha = new /obj/hoota(locate(10, 10, 1))
	for(var/i = 1 to 50) new /obj/filler(locate(10, 10, 1))
	var/obj/ta = new /obj/toota(locate(4, 4, 1))
	for(var/i = 1 to 47) new /obj/filler(locate(4, 4, 1))
	world.log << "BOOT hb \ref[hb] tb \ref[tb] ha \ref[ha] ta \ref[ta]"
