// Fugue Verb Gong: objs and mobs reach a cell in an order that follows neither ascending nor descending ref. The player stands at 7,7.
// Gong 9,9: ga (highest ref) arrives first, gb (lowest) second, gc (middle) last.
// Hum 5,5: h1 (ref r+3), h2 (r), h3 (r+2), h4 (r+1), in that order.
// Yap 5,9: npc y1 at boot, y2 (lower mob ref) arrives, then y3 (lowest); last, y1.loc = y1.loc.
/obj/gong
	name = "gonger"
/obj/gong/verb/gong()
	set name = "Gong"
	set src = view(5)
	world.log << "VERB Gong [name] \ref[src]"
/obj/hum
	name = "hummer"
/obj/hum/verb/hum()
	set name = "Hum"
	set src = view(5)
	world.log << "VERB Hum [name] \ref[src]"
/mob/npc
	name = "npc"
/mob/npc/verb/yap()
	set name = "Yap"
	set src = view(5)
	world.log << "VERB Yap [tag] \ref[src]"
var/obj/gong/ga
var/obj/gong/gb
var/obj/gong/gc
var/obj/hum/h1
var/obj/hum/h2
var/obj/hum/h3
var/obj/hum/h4
var/mob/npc/y1
var/mob/npc/y2
var/mob/npc/y3
var/started = 0
world
	name = "Fugue Verb Gong"
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
		ga.loc = locate(9, 9, 1)
		h1.loc = locate(5, 5, 1)
		y2.loc = locate(5, 9, 1)
		world.log << "T1"
		sleep(10)
		gb.loc = locate(9, 9, 1)
		h2.loc = locate(5, 5, 1)
		y3.loc = locate(5, 9, 1)
		world.log << "T2"
		sleep(10)
		gc.loc = locate(9, 9, 1)
		h3.loc = locate(5, 5, 1)
		world.log << "T3"
		sleep(10)
		h4.loc = locate(5, 5, 1)
		y1.loc = y1.loc
		world.log << "T4"
/world/New()
	..()
	gb = new /obj/gong(locate(18, 1, 1))
	gb.name = "gonger"
	gc = new /obj/gong(locate(18, 2, 1))
	ga = new /obj/gong(locate(18, 3, 1))
	h2 = new /obj/hum(locate(18, 4, 1))
	h4 = new /obj/hum(locate(18, 5, 1))
	h3 = new /obj/hum(locate(18, 6, 1))
	h1 = new /obj/hum(locate(18, 7, 1))
	y1 = new /mob/npc(locate(5, 9, 1))
	y1.tag = "y1"
	y3 = new /mob/npc(locate(18, 8, 1))
	y3.tag = "y3"
	y2 = new /mob/npc(locate(18, 9, 1))
	y2.tag = "y2"
	world.log << "BOOT ga \ref[ga] gb \ref[gb] gc \ref[gc] h1 \ref[h1] h2 \ref[h2] h3 \ref[h3] h4 \ref[h4] y1 \ref[y1] y2 \ref[y2] y3 \ref[y3]"
