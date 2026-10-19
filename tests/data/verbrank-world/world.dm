// Fugue Verb Rank: same-named verbs on different srcs, each run by its typed name.
world
	name = "Fugue Verb Rank"
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

// A client + mob
/client/verb/ping_c()
	set name = "Ping"
	world.log << "VERB Ping client"
/mob/verb/ping_m()
	set name = "Ping"
	world.log << "VERB Ping mob [src]"

// B obj(view) + mob
/obj/verb/grab_o()
	set name = "Grab"
	set src in view()
	world.log << "VERB Grab obj [src]"
/mob/verb/grab_m()
	set name = "Grab"
	world.log << "VERB Grab mob [src]"

// C client + obj(view), obj first
/obj/box/verb/tap_o()
	set name = "Tap"
	set src in view()
	world.log << "VERB Tap obj [src]"
/client/verb/tap_c()
	set name = "Tap"
	world.log << "VERB Tap client"

// D two obj types, distinct verbs
/obj/box/verb/knock_b()
	set name = "Knock"
	set src in view()
	world.log << "VERB Knock box [src]"
/obj/crate/verb/knock_c()
	set name = "Knock"
	set src in view()
	world.log << "VERB Knock crate [src]"

// E obj override
/obj/verb/use_it()
	set name = "Use It"
	set src in view()
	world.log << "VERB UseIt obj [src]"
/obj/crate/use_it()
	world.log << "VERB UseIt crate [src]"

// F one mob verb in view: own mob + npc share the id
/mob/verb/wave()
	set name = "Wave"
	set src in view()
	world.log << "VERB Wave [src]"

// G own mob verb + npc's distinct verb in view
/mob/verb/nudge_m()
	set name = "Nudge"
	world.log << "VERB Nudge self [src]"
/mob/npc/verb/nudge_n()
	set name = "Nudge"
	set src in view()
	world.log << "VERB Nudge npc [src]"

// H turf + mob
/turf/verb/stomp_t()
	set name = "Stomp"
	world.log << "VERB Stomp turf [src] [x],[y]"
/mob/verb/stomp_m()
	set name = "Stomp"
	world.log << "VERB Stomp mob [src]"

// I area + mob
/area/verb/shout_a()
	set name = "Shout"
	world.log << "VERB Shout area [src]"
/mob/verb/shout_m()
	set name = "Shout"
	world.log << "VERB Shout mob [src]"

// J obj in usr + obj in view
/obj/item/verb/pack_i()
	set name = "Pack"
	world.log << "VERB Pack item [src]"
/obj/box/verb/pack_b()
	set name = "Pack"
	set src in view()
	world.log << "VERB Pack box [src]"

// K turf + obj(view)
/turf/verb/kick_t()
	set name = "Kick"
	set src in view(1)
	world.log << "VERB Kick turf [x],[y]"
/obj/box/verb/kick_b()
	set name = "Kick"
	set src in view()
	world.log << "VERB Kick box [src]"

// L mob override, in view: own human + npc (/mob base id)
/mob/verb/look()
	set name = "Look"
	set src in view()
	world.log << "VERB Look mob [src]"
/mob/living/look()
	world.log << "VERB Look living [src]"

// M arguments: client(num) + mob(text)
/client/verb/calc_c(n as num)
	set name = "Calc"
	world.log << "VERB Calc client [n]"
/mob/verb/calc_m(t as text)
	set name = "Calc"
	world.log << "VERB Calc mob [t]"

// N arguments: box(num) + crate(text), both in view
/obj/box/verb/fold_b(n as num)
	set name = "Fold"
	set src in view()
	world.log << "VERB Fold box [n]"
/obj/crate/verb/fold_c(t as text)
	set name = "Fold"
	set src in view()
	world.log << "VERB Fold crate [t]"

// O client + turf
/client/verb/dig_c()
	set name = "Dig"
	world.log << "VERB Dig client"
/turf/verb/dig_t()
	set name = "Dig"
	world.log << "VERB Dig turf [x],[y]"

// P obj in usr + mob
/obj/item/verb/drop_i()
	set name = "Drop"
	world.log << "VERB Drop item [src]"
/mob/verb/drop_m()
	set name = "Drop"
	world.log << "VERB Drop mob [src]"

// Q npc(view) + obj(view)
/mob/npc/verb/pet_n()
	set name = "Pet"
	set src in view()
	world.log << "VERB Pet npc [src]"
/obj/box/verb/pet_b()
	set name = "Pet"
	set src in view()
	world.log << "VERB Pet box [src]"

// R two npcs: distinct verbs on two mob types
/mob/npc/verb/hail_n()
	set name = "Hail"
	set src in view()
	world.log << "VERB Hail npc [src]"
/mob/cat/verb/hail_c()
	set name = "Hail"
	set src in view()
	world.log << "VERB Hail cat [src]"

// S client + area
/client/verb/glow_c()
	set name = "Glow"
	world.log << "VERB Glow client"
/area/verb/glow_a()
	set name = "Glow"
	world.log << "VERB Glow area [src]"

// T obj in usr + client
/obj/item/verb/toss_i()
	set name = "Toss"
	world.log << "VERB Toss item [src]"
/client/verb/toss_c()
	set name = "Toss"
	world.log << "VERB Toss client"

/mob/living/carbon/human
/mob/npc
	name = "npc"
/mob/cat
	name = "cat"
/obj/box
	name = "box"
/obj/crate
	name = "crate"
/obj/item
	name = "pebble"

/mob/Login()
	if(!loc) loc = locate(4, 4, 1)
	if(!(locate(/obj/item) in src)) new /obj/item(src)
	..()

/world/New()
	..()
	new /obj/box(locate(5, 4, 1))
	new /obj/crate(locate(4, 5, 1))
	new /mob/npc(locate(3, 4, 1))
	new /mob/cat(locate(4, 3, 1))

/mob/verb/zap_m(n as num)
	set name = "Zap"
	world.log << "VERB Zap mob [n]"
/client/verb/zap_c(t as text)
	set name = "Zap"
	world.log << "VERB Zap client [t]"
/mob/verb/hop_m(t as text)
	set name = "Hop"
	world.log << "VERB Hop mob [t]"
/client/verb/hop_c(t as text)
	set name = "Hop"
	world.log << "VERB Hop client [t]"
/mob/verb/bow_m()
	set name = "Bow"
	world.log << "VERB Bow mob"
/client/verb/bow_c(t as text)
	set name = "Bow"
	world.log << "VERB Bow client [t]"
/mob/verb/nod_m(t as text)
	set name = "Nod"
	world.log << "VERB Nod mob [t]"
/client/verb/nod_c()
	set name = "Nod"
	world.log << "VERB Nod client"
/turf/verb/spin_t()
	set name = "Spin"
	world.log << "VERB Spin turf [x],[y]"
/area/verb/spin_a()
	set name = "Spin"
	world.log << "VERB Spin area"
/turf/verb/flip_t()
	set name = "Flip"
	world.log << "VERB Flip turf [x],[y]"
/obj/box/verb/flip_b()
	set name = "Flip"
	set src in view()
	world.log << "VERB Flip box"
/area/verb/roll_a()
	set name = "Roll"
	world.log << "VERB Roll area"
/obj/box/verb/roll_b()
	set name = "Roll"
	set src in view()
	world.log << "VERB Roll box"
/turf/verb/rub_t()
	set name = "Rub"
	world.log << "VERB Rub turf [x],[y]"
/obj/item/verb/rub_i()
	set name = "Rub"
	world.log << "VERB Rub item"
/obj/box/verb/lick_b()
	set name = "Lick"
	set src = view(1)
	world.log << "VERB Lick box"
/obj/crate/verb/lick_c()
	set name = "Lick"
	set src in view(1)
	world.log << "VERB Lick crate"
/obj/box/verb/sniff_b()
	set name = "Sniff"
	set src = view(1)
	world.log << "VERB Sniff box"
/client/verb/sniff_c()
	set name = "Sniff"
	world.log << "VERB Sniff client"
/mob/verb/duo_a()
	set name = "Duo"
	world.log << "VERB Duo a"
/mob/verb/duo_b()
	set name = "Duo"
	world.log << "VERB Duo b"
/client/verb/trio_a()
	set name = "Trio"
	world.log << "VERB Trio a"
/client/verb/trio_b()
	set name = "Trio"
	world.log << "VERB Trio b"
/mob/verb/yell_m(t as text)
	set name = "Yell"
	world.log << "VERB Yell self [t]"
/mob/npc/verb/yell_n(t as text)
	set name = "Yell"
	set src in view()
	world.log << "VERB Yell npc [src] [t]"
/mob/verb/beam(t as text)
	set name = "Beam"
	set src in view()
	world.log << "VERB Beam [src] [t]"
/mob/verb/hum_m()
	set name = "Hum"
	set src = usr
	world.log << "VERB Hum self"
/obj/box/verb/hum_b()
	set name = "Hum"
	set src = view(1)
	world.log << "VERB Hum box"
/mob/verb/clap_m()
	set name = "Clap"
	set src = view(1)
	world.log << "VERB Clap mob [src]"
/client/verb/clap_c()
	set name = "Clap"
	world.log << "VERB Clap client"

/mob/verb/purr_m(t as text)
	set name = "Purr"
	world.log << "VERB Purr mob [t]"
/client/verb/purr_c(t as null|text)
	set name = "Purr"
	world.log << "VERB Purr client [t]"
/mob/verb/moo_m(n as num)
	set name = "Moo"
	world.log << "VERB Moo mob [n]"
/client/verb/moo_c(n as num)
	set name = "Moo"
	world.log << "VERB Moo client [n]"
/mob/verb/quack_m(t as text)
	set name = "Quack"
	world.log << "VERB Quack mob [t]"
/area/verb/quack_a(n as num)
	set name = "Quack"
	world.log << "VERB Quack area [n]"
/client/verb/oink_c(t as text)
	set name = "Oink"
	world.log << "VERB Oink client [t]"
/area/verb/oink_a(n as num)
	set name = "Oink"
	world.log << "VERB Oink area [n]"
/mob/verb/neigh_m(t as text)
	set name = "Neigh"
	world.log << "VERB Neigh mob [t]"
/turf/verb/neigh_t(n as num)
	set name = "Neigh"
	world.log << "VERB Neigh turf [n]"
/turf/verb/baa_t(t as text)
	set name = "Baa"
	world.log << "VERB Baa turf [t]"
/mob/verb/baa_m(n as num)
	set name = "Baa"
	world.log << "VERB Baa mob [n]"
/mob/verb/cluck_m(t as text)
	set name = "Cluck"
	world.log << "VERB Cluck mob [t]"
/obj/box/verb/cluck_b(n as num)
	set name = "Cluck"
	set src = view(1)
	world.log << "VERB Cluck box [n]"
/turf/verb/tweet_t(t as text)
	set name = "Tweet"
	world.log << "VERB Tweet turf [t]"
/area/verb/tweet_a(n as num)
	set name = "Tweet"
	world.log << "VERB Tweet area [n]"
/mob/verb/mew(t as null|text)
	set name = "Mew"
	world.log << "VERB Mew [t]"
