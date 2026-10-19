// Fugue Band Pairs: two atoms per cell, a horizontal and a vertical one, created in both orders.
world
	name = "Fugue Band Pairs"
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
// horizontal kind, layer, vertical kind, layer
var/list/cases = list(
	"s", 1, "s", 4999,
	"s", 4999, "s", 1,
	"s", 1, "m", 5000.00049,
	"s", 4999, "m", 5000.00049,
	"s", 21000, "m", 25000.5,
	"s", 24999, "m", 25000.5,
	"s", 5000, "m", 19999,
	"s", 20001, "s", 24999,
	"s", 24999, "s", 20001,
	"s", 5000, "s", 15000,
	"s", 15000, "s", 5000,
	"s", 1, "m", 5000,
	"m", 1, "m", 20000,
	"s", 2, "m", 5000,
	"s", 3, "m", 5000,
	"s", 4, "m", 5000,
	"s", 20001, "m", 25000,
	"s", 20002, "m", 25000,
	"s", 20003, "m", 25000,
	"s", 20004, "m", 25000,
	"s", 5000, "m", 10000,
	"s", 5001, "m", 10000,
	"s", 1, "m", 4999,
	"s", 1, "s", 1)
mob/player/proc/mk(nm, kind, shape, ly, x, y)
	var/obj/a/o = new
	o.icon_state = "[kind][shape]"
	o.name = nm
	o.layer = ly
	if(kind == "s")
		o.screen_loc = "[x],[y]"
		client.screen += o
	else
		o.loc = locate(x, y, 1)
mob/player/Login()
	..()
	loc = locate(8, 8, 1)
	name = "player"
	var/n = 0
	for(var/y = 2, y <= 14, y += 2)
		for(var/x = 2, x <= 14, x += 2)
			if(x == 8 && y == 8) continue
			var/c = round(n / 2)
			if(c * 4 + 4 > cases.len) break
			var/i = c * 4
			if(n % 2)
				mk("c[c]bv", cases[i+3], "v", cases[i+4], x, y)
				mk("c[c]bh", cases[i+1], "h", cases[i+2], x, y)
			else
				mk("c[c]ah", cases[i+1], "h", cases[i+2], x, y)
				mk("c[c]av", cases[i+3], "v", cases[i+4], x, y)
			world.log << "CASE c[c][n % 2 ? "b" : "a"] [x] [y] [cases[i+1]][cases[i+2]] [cases[i+3]][cases[i+4]] [n % 2]"
			n++
mob/verb/examination(atom/A as mob|obj|turf in view(src))
	set name = "Examine"
	world.log << "EXAMINE [A]"
