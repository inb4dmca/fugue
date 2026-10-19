// Fugue Band Pixels: one map obj and one screen obj per cell, both creation orders.
world
	name = "Fugue Band Pixels"
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
obj/thing
	icon = 'thing.dmi'
	icon_state = "item"
obj/scr
	icon = 'thing.dmi'
	icon_state = "bar"
var/list/cases = list(
	5000, 1,  5000, 4999,  5000, 5000,  4999, 1,
	20000, 1,  20000, 5000,  20000, 19999,  20000, 20000,
	25000, 20001,  25000, 20002,  25000, 24000,  25000, 23999,
	25000, 24999,  25000, 25000,  24999, 20001,  5001, 1,
	5000.5, 4999,  20000.5, 1,  15000, 5000,  30000, 24000,
	26000, 24000,  25000.5, 24000,  10000, 10000,  45000, 20001)
mob/player/proc/mk(nm, ml, sl, x, y, sfirst)
	var/obj/scr/b
	if(sfirst)
		b = new
		b.name = "[nm]s"
		b.layer = sl
		b.screen_loc = "[x],[y]"
		client.screen += b
	var/obj/thing/t = new(locate(x, y, 1))
	t.name = "[nm]m"
	t.layer = ml
	if(!sfirst)
		b = new
		b.name = "[nm]s"
		b.layer = sl
		b.screen_loc = "[x],[y]"
		client.screen += b
	world.log << "CASE [nm] [x] [y] [ml] [sl] [sfirst]"
mob/player/Login()
	..()
	loc = locate(8, 8, 1)
	name = "player"
	var/n = 0
	for(var/y = 2, y <= 14, y += 2)
		for(var/x = 2, x <= 14, x += 2)
			if(x == 8 && y == 8) continue
			var/c = round(n / 2)
			if(c * 2 + 2 > cases.len) break
			mk("c[c][n % 2 ? "b" : "a"]", cases[c * 2 + 1], cases[c * 2 + 2], x, y, n % 2)
			n++
mob/verb/examination(atom/A as mob|obj|turf in view(src))
	set name = "Examine"
	world.log << "EXAMINE [A]"
