// Fugue Band Background: BACKGROUND_LAYER map objs alone and in company.
world
	name = "Fugue Band Background"
	view = 7
	turf = /turf/floor
	mob = /mob/player
	maxx = 15
	maxy = 15
	maxz = 1
turf/floor
	icon = 'thing.dmi'
	icon_state = "floor"
mob/player
	icon = 'thing.dmi'
	icon_state = "player"
obj/a
	icon = 'thing.dmi'
// per cell: mode (0 = new at loc then layer, 1 = layer then loc), then up to two atoms: kind, shape, layer ("" = none)
var/list/cases = list(
	0, "m", "v", 20000, "", "", 0,
	1, "m", "v", 20000, "", "", 0,
	0, "m", "v", 20000, "m", "h", 1,
	1, "m", "v", 20000, "m", "h", 1,
	0, "m", "v", 20000, "s", "h", 1,
	1, "m", "v", 20000, "s", "h", 1,
	0, "m", "v", 25000, "", "", 0,
	0, "m", "v", 20000.5, "", "", 0,
	0, "m", "v", 19999, "", "", 0,
	0, "m", "v", 20000, "m", "h", 3,
	0, "s", "v", 20000, "", "", 0,
	0, "s", "v", 20000, "m", "h", 1)
mob/player/proc/mk(nm, kind, shape, ly, x, y, mode)
	var/obj/a/o
	if(kind == "m" && !mode)
		o = new(locate(x, y, 1))
	else
		o = new
	o.icon_state = "[kind][shape]"
	o.name = nm
	o.layer = ly
	if(kind == "s")
		o.screen_loc = "[x],[y]"
		client.screen += o
	else if(mode)
		o.loc = locate(x, y, 1)
mob/player/Login()
	..()
	loc = locate(8, 8, 1)
	name = "player"
	var/n = 0
	for(var/y = 2, y <= 14, y += 2)
		for(var/x = 2, x <= 14, x += 2)
			if(x == 8 && y == 8) continue
			var/c = n % 12
			var/i = c * 7
			mk("n[n]v", cases[i+2], cases[i+3], cases[i+4], x, y, cases[i+1])
			if(cases[i+5] != "") mk("n[n]h", cases[i+5], cases[i+6], cases[i+7], x, y, cases[i+1])
			world.log << "CASE n[n] [x] [y] [cases[i+2]][cases[i+4]] [cases[i+5]][cases[i+7]] [cases[i+1]]"
			n++
mob/verb/examination(atom/A as mob|obj|turf in view(src))
	set name = "Examine"
	world.log << "EXAMINE [A]"
