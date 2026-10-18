// HUDHITS: client.screen objects larger than one tile, drawn and clicked whole. Every object is on plane 24, beside a plane
// master unless NO_PM is defined (world-nopm.dme). Click() logs the params stock DreamSeeker sends. Icons: mkdmi.py.
//   btnA     64x32 at CENTER+3,CENTER-4, transparent at icon x 49..56 (vg's interface buttons)
//   btnB     64x32 at CENTER-0.5,CENTER-4, straddling cells
//   bigC     96x96 at WEST+1:8,NORTH-4:4, its top-right 32x32 transparent
//   holderD  iconless, at EAST-4,NORTH-4, with a 96x96 client.image whose top-left 32x32 is transparent (vg's holomap)
//   slotE    an ordinary 32x32 slot at EAST,SOUTH
world
	name = "Fugue HUD Hits"
	view = 7
	turf = /turf/floor
	mob = /mob/player
	maxx = 30
	maxy = 30
	maxz = 1

turf/floor
	icon = 'thing.dmi'
	icon_state = "floor"
turf/floor/Click(loc, ctl, params)
	world.log << "CLICK turf [x],[y] [params]"

obj/scr
	plane = 24
	layer = 5
obj/scr/Click(loc, ctl, params)
	world.log << "CLICK [name] [params]"

obj/scr/master
	appearance_flags = PLANE_MASTER
	screen_loc = "CENTER"
	plane = 10
	mouse_opacity = 0

mob/player
	icon = 'thing.dmi'
	icon_state = "player"

mob/player/proc/mk(nm, I, st, loc)
	var/obj/scr/o = new
	o.name = nm
	o.icon = I
	o.icon_state = st
	o.screen_loc = loc
	client.screen += o

mob/player/Login()
	..()
	loc = locate(15, 15, 1)
#ifndef NO_PM
	client.screen += new /obj/scr/master
#endif
	mk("btnA", 'wide.dmi', "a", "CENTER+3,CENTER-4")
	mk("btnB", 'wide.dmi', "b", "CENTER-0.5,CENTER-4")
	mk("bigC", 'big.dmi', "c", "WEST+1:8,NORTH-4:4")
	var/obj/scr/D = new
	D.name = "holderD"
	D.screen_loc = "EAST-4,NORTH-4"
	client.screen += D
	client.images += image('big.dmi', D, "d")
	mk("slotE", 'thing.dmi', "slot", "EAST,SOUTH")
