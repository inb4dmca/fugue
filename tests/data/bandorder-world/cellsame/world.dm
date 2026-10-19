// Fugue Cell Order: equal-layer map objs overlapping across cells, north/south and east/west.
world
	name = "Fugue Cell Order"
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
mob/player/proc/mk(st, x, y, px, py, ly)
	var/obj/a/o = new(locate(x, y, 1))
	o.icon_state = st
	o.pixel_x = px
	o.pixel_y = py
	o.layer = ly
mob/player/Login()
	..()
	loc = locate(8, 8, 1)
	name = "player"
	// north/south: north first, then south first
	mk("mh", 3, 5, 0, -16, 3); mk("mv", 3, 4, 0, 16, 3)
	mk("mv", 5, 4, 0, 16, 3); mk("mh", 5, 5, 0, -16, 3)
	// east/west
	mk("mh", 3, 11, 16, 0, 3); mk("mv", 4, 11, -16, 0, 3)
	mk("mv", 6, 11, -16, 0, 3); mk("mh", 5, 11, 16, 0, 3)
	// a layer gap smaller than the tie-break: north 3.0001 over south 3?
	mk("mh", 11, 5, 0, -16, 3.0001); mk("mv", 11, 4, 0, 16, 3)
	mk("mh", 13, 5, 0, -16, 3.003); mk("mv", 13, 4, 0, 16, 3)
	var/obj/a/t = new(locate(15, 15, 1))
	t.icon_state = "mh"
	t.name = "ticker"
	spawn while(1)
		t.pixel_x = (t.pixel_x + 1) % 8
		sleep(1)
