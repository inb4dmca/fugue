// Fugue Verb Near: same-named verbs whose srcs differ only in distance, footprint or stacking. The player stands at 6,6.
// Buzz: both srcs need no word, the lower id farther (far 9,6; near 7,6).
/obj/far/verb/buzz_f()
	set name = "Buzz"
	set src = view(3)
	world.log << "VERB Buzz far [x],[y]"
/obj/far
	name = "faraway"
/obj/near/verb/buzz_n()
	set name = "Buzz"
	set src = view(3)
	world.log << "VERB Buzz near [x],[y]"
/obj/near
	name = "nearby"
// Hum: one verb, two atoms of one type, the lower ref farther (6,9 and 5,6).
/obj/hum/verb/hum_h()
	set name = "Hum"
	set src = view(3)
	world.log << "VERB Hum [x],[y]"
/obj/hum
	name = "hummer"
// Tap: a src word both verbs accept, at two distances (pad a 3,3; pad b 6,7).
/obj/tapa/verb/tap_a()
	set name = "Tap"
	set src in view(3)
	world.log << "VERB Tap a [x],[y]"
/obj/tapa
	name = "pad"
/obj/tapb/verb/tap_b()
	set name = "Tap"
	set src in view(3)
	world.log << "VERB Tap b [x],[y]"
/obj/tapb
	name = "pad"
// Ring: one tile each; ring a (lower id) shares 7,5 with a mob, ring b is alone at 5,5.
/obj/ringa/verb/ring_a()
	set name = "Ring"
	set src = view(3)
	world.log << "VERB Ring a [x],[y]"
/obj/ringa
	name = "ringer"
/obj/ringb/verb/ring_b()
	set name = "Ring"
	set src = view(3)
	world.log << "VERB Ring b [x],[y]"
/obj/ringb
	name = "ringer"
// Bell: both on 8,8, bell a (lower id) made first.
/obj/bella/verb/bell_a()
	set name = "Bell"
	set src = view(3)
	world.log << "VERB Bell a [x],[y]"
/obj/bella
	name = "bell"
/obj/bellb/verb/bell_b()
	set name = "Bell"
	set src = view(3)
	world.log << "VERB Bell b [x],[y]"
/obj/bellb
	name = "bell"
// Gong: both on 4,8, gong a (lower id) made second.
/obj/gonga/verb/gong_a()
	set name = "Gong"
	set src = view(3)
	world.log << "VERB Gong a [x],[y]"
/obj/gonga
	name = "gong"
/obj/gongb/verb/gong_b()
	set name = "Gong"
	set src = view(3)
	world.log << "VERB Gong b [x],[y]"
/obj/gongb
	name = "gong"
// Ping: ping a (lower id) at 4,6; ping b at 10,6 with bounds reaching back to 7,6.
/obj/pinga/verb/ping_a()
	set name = "Ping"
	set src = view(4)
	world.log << "VERB Ping a [x],[y]"
/obj/pinga
	name = "pinger"
/obj/pingb/verb/ping_b()
	set name = "Ping"
	set src = view(4)
	world.log << "VERB Ping b [x],[y] [bound_x] [bound_width]"
/obj/pingb
	name = "pinger"
// Purr: purr a (lower id) at 8,6; purr b at 3,6 drawn 64 px east (pixel_x).
/obj/purra/verb/purr_a()
	set name = "Purr"
	set src = view(4)
	world.log << "VERB Purr a [x],[y]"
/obj/purra
	name = "purrer"
/obj/purrb/verb/purr_b()
	set name = "Purr"
	set src = view(4)
	world.log << "VERB Purr b [x],[y]"
/obj/purrb
	name = "purrer"
// Hiss: hiss a (lower id) at 6,4; hiss b at 6,8 stepped 16 px south (step_y).
/obj/hissa/verb/hiss_a()
	set name = "Hiss"
	set src = view(4)
	world.log << "VERB Hiss a [x],[y]"
/obj/hissa
	name = "hisser"
/obj/hissb/verb/hiss_b()
	set name = "Hiss"
	set src = view(4)
	world.log << "VERB Hiss b [x],[y] [step_y]"
/obj/hissb
	name = "hisser"
// Toss: both carried; toss b enters usr.contents first, toss a (lower id, lower ref) a second later.
/obj/tossa/verb/toss_a()
	set name = "Toss"
	set src = usr.contents
	world.log << "VERB Toss a"
/obj/tossa
	name = "tosser"
/obj/tossb/verb/toss_b()
	set name = "Toss"
	set src = usr.contents
	world.log << "VERB Toss b"
/obj/tossb
	name = "tosser"
// Aim: an obj argument, two pads at two distances.
/mob/verb/aim(obj/o as obj in view(4))
	world.log << "VERB Aim [o.type] [o.x],[o.y]"

/mob/npc
	name = "npc"

// Zing: one tile each, each alone on its cell; zing a (lower id) has the lower ref.
/obj/zinga/verb/zing_a()
	set name = "Zing"
	set src = view(3)
	world.log << "VERB Zing a [x],[y]"
/obj/zinga
	name = "zinger"
/obj/zingb/verb/zing_b()
	set name = "Zing"
	set src = view(3)
	world.log << "VERB Zing b [x],[y]"
/obj/zingb
	name = "zinger"
// Clap: one tile each; clap a (lower id, higher ref) shares 4,4 with a mob, clap b is alone at 8,4.
/obj/clapa/verb/clap_a()
	set name = "Clap"
	set src = view(3)
	world.log << "VERB Clap a [x],[y]"
/obj/clapa
	name = "clapper"
/obj/clapb/verb/clap_b()
	set name = "Clap"
	set src = view(3)
	world.log << "VERB Clap b [x],[y]"
/obj/clapb
	name = "clapper"
// Flip: both carried; flip b (lower ref) enters usr.contents first, flip a a second later.
/obj/flipa/verb/flip_a()
	set name = "Flip"
	set src = usr.contents
	world.log << "VERB Flip a"
/obj/flipa
	name = "flipper"
/obj/flipb/verb/flip_b()
	set name = "Flip"
	set src = usr.contents
	world.log << "VERB Flip b"
/obj/flipb
	name = "flipper"

var/obj/flipa/flipLater
var/obj/flipb/flipFirst
var/obj/tossa/later
world
	name = "Fugue Verb Near"
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
	if(!(locate(/obj/tossb) in src)) new /obj/tossb(src)
	if(flipFirst && flipFirst.loc != src) flipFirst.loc = src
	spawn(10)
		if(later && later.loc != src)
			later.loc = src
			world.log << "TOSS a carried"
		if(flipLater && flipLater.loc != src)
			flipLater.loc = src
			world.log << "FLIP a carried"
/world/New()
	..()
	new /obj/far(locate(9, 6, 1))
	new /obj/near(locate(7, 6, 1))
	new /obj/hum(locate(6, 9, 1))
	new /obj/hum(locate(5, 6, 1))
	new /obj/tapa(locate(3, 3, 1))
	new /obj/tapb(locate(6, 7, 1))
	new /mob/npc(locate(7, 5, 1))
	new /obj/ringa(locate(7, 5, 1))
	new /obj/ringb(locate(5, 5, 1))
	new /obj/bella(locate(8, 8, 1))
	new /obj/bellb(locate(8, 8, 1))
	new /obj/gongb(locate(4, 8, 1))
	new /obj/gonga(locate(4, 8, 1))
	new /obj/pinga(locate(4, 6, 1))
	var/obj/p = new /obj/pingb(locate(10, 6, 1))
	p.bound_x = -96
	p.bound_width = 128
	new /obj/purra(locate(8, 6, 1))
	p = new /obj/purrb(locate(3, 6, 1))
	p.pixel_x = 64
	new /obj/hissa(locate(6, 4, 1))
	p = new /obj/hissb(locate(6, 8, 1))
	p.step_y = -16
	later = new /obj/tossa(locate(1, 1, 1))
	new /obj/zinga(locate(8, 7, 1))
	new /obj/zingb(locate(4, 5, 1))
	new /obj/clapb(locate(8, 4, 1))
	new /mob/npc(locate(4, 4, 1))
	new /obj/clapa(locate(4, 4, 1))
	flipFirst = new /obj/flipb(locate(1, 2, 1))
	flipLater = new /obj/flipa(locate(2, 1, 1))
	world.log << "pingb [p.x] hissb [p.x],[p.y] step [p.step_y]"
