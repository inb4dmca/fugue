// Fugue Verb Rank Reversed: Fugue Verb Rank's same-named pairs with the owners declared in reverse (obj, mob, area,
// client, turf), so DreamDaemon numbers the verbs the other way round.
/obj/box/verb/cluck_b(n as num)
	set name = "Cluck"
	set src = view(1)
	world.log << "VERB Cluck box [n]"
/obj/box
	name = "box"
/mob/verb/cluck_m(t as text)
	set name = "Cluck"
	world.log << "VERB Cluck mob [t]"
/mob/verb/neigh_m(t as text)
	set name = "Neigh"
	world.log << "VERB Neigh mob [t]"
/mob/verb/calc_m(t as text)
	set name = "Calc"
	world.log << "VERB Calc mob [t]"
/mob/verb/quack_m(t as text)
	set name = "Quack"
	world.log << "VERB Quack mob [t]"
/mob/verb/ping_m()
	set name = "Ping"
	world.log << "VERB Ping mob"
/area/verb/quack_a(n as num)
	set name = "Quack"
	world.log << "VERB Quack area [n]"
/area/verb/tweet_a(n as num)
	set name = "Tweet"
	world.log << "VERB Tweet area [n]"
/client/verb/calc_c(n as num)
	set name = "Calc"
	world.log << "VERB Calc client [n]"
/client/verb/oink_c(t as text)
	set name = "Oink"
	world.log << "VERB Oink client [t]"
/client/verb/ping_c()
	set name = "Ping"
	world.log << "VERB Ping client"
/client/verb/dig_c()
	set name = "Dig"
	world.log << "VERB Dig client"
/turf/verb/neigh_t(n as num)
	set name = "Neigh"
	world.log << "VERB Neigh turf [n]"
/turf/verb/tweet_t(t as text)
	set name = "Tweet"
	world.log << "VERB Tweet turf [t]"
/turf/verb/oink_t(n as num)
	set name = "Oink"
	world.log << "VERB Oink turf [n]"
/turf/verb/dig_t()
	set name = "Dig"
	world.log << "VERB Dig turf"
world
	name = "Fugue Verb Rank Reversed"
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
/world/New()
	..()
	new /obj/box(locate(5, 4, 1))
/obj/box/verb/sniff_b()
	set name = "Sniff"
	set src = view(1)
	world.log << "VERB Sniff box"
/area/verb/spin_a()
	set name = "Spin"
	world.log << "VERB Spin area"
/area/verb/glow_a()
	set name = "Glow"
	world.log << "VERB Glow area"
/client/verb/sniff_c()
	set name = "Sniff"
	world.log << "VERB Sniff client"
/client/verb/glow_c()
	set name = "Glow"
	world.log << "VERB Glow client"
/turf/verb/spin_t()
	set name = "Spin"
	world.log << "VERB Spin turf"
