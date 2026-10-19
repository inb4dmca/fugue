// Fugue Cell Order Two: equal-layer map objs overlapping across two tie-break cells.
world
	name = "Fugue Cell Order Two"
	view = 7
	turf = /turf/floor
	mob = /mob/player
	maxx = 15
	maxy = 15
	maxz = 1
	fps = 10
turf/floor
	icon = 'thing.dmi'
	icon_state = "floor"
mob/player
	icon = 'thing.dmi'
	icon_state = "player"
obj/a
	icon = 'thing.dmi'
	layer = 3
mob/player/proc/mk(st, x, y, px, py)
	var/obj/a/o = new(locate(x, y, 1))
	o.icon_state = st
	o.pixel_x = px
	o.pixel_y = py
	return o
mob/player/Login()
	..()
	loc = locate(8, 8, 1)
	name = "player"
	// west +16 over east 0; east first, then west first
	mk("mv", 4, 11, 0, 0); mk("mh", 3, 11, 16, 0)
	mk("mh", 5, 11, 16, 0); mk("mv", 6, 11, 0, 0)
	// south +16 under north 0; both creation orders
	mk("mh", 3, 5, 0, 0); mk("mv", 3, 4, 0, 16)
	mk("mv", 5, 4, 0, 16); mk("mh", 5, 5, 0, 0)
	// an overlay with its own pixel_x 16 against the east neighbour
	var/obj/a/o = mk("mv", 10, 11, 0, 0)
	var/image/I = image('thing.dmi', icon_state = "mh")
	I.pixel_x = 16
	o = mk("blank", 9, 11, 0, 0)
	o.overlays += I
	// an overlay with its own pixel_x -16 against the west neighbour
	o = mk("mv", 12, 11, 0, 0)
	I = image('thing.dmi', icon_state = "mh")
	I.pixel_x = -16
	o = mk("blank", 13, 11, 0, 0)
	o.overlays += I
	var/obj/a/t = new(locate(15, 15, 1))
	t.icon_state = "mh"
	t.name = "ticker"
	spawn while(1)
		t.pixel_x = (t.pixel_x + 1) % 8
		sleep(1)
