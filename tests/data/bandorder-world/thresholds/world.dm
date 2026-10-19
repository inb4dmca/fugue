// Fugue Band Thresholds: one map obj and one screen obj per cell.
world
	name = "Fugue Band Thresholds"
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
	"p0", 2, 2, 9999, 1,
	"p1", 4, 2, 10000, 1,
	"p2", 6, 2, 10001, 1,
	"p3", 8, 2, 12000, 1,
	"p4", 10, 2, 14999, 1,
	"p5", 12, 2, 15000, 1,
	"p6", 14, 2, 15001, 1,
	"p7", 2, 4, 19999, 1,
	"p8", 4, 4, 5001, 1,
	"p9", 6, 4, 15000, 5001,
	"p10", 8, 4, 14000, 1,
	"p11", 10, 4, 10000.5, 1,
	"p12", 12, 4, 15000.5, 1,
	"p13", 14, 4, 13000, 4000,
	"p14", 2, 6, 24999, 20001,
	"p15", 4, 6, 25000, 20001,
	"p16", 6, 6, 25001, 20001,
	"p17", 8, 6, 29999, 20001,
	"p18", 10, 6, 30000, 20001,
	"p19", 12, 6, 30001, 20001,
	"p20", 14, 6, 35001, 20001,
	"p21", 2, 10, 40000, 20001,
	"p22", 4, 10, 22000, 20001,
	"p23", 6, 10, 5000, 1,
	"p24", 10, 10, 25000, 20002,
	"p25", 12, 10, 25000, 24000,
	"p26", 14, 10, 15000, 10000,
	"p27", 2, 12, 15000, 9000)
mob/player/Login()
	..()
	loc = locate(8, 8, 1)
	name = "player"
	for(var/i = 1, i <= cases.len, i += 5)
		var/obj/thing/t = new(locate(cases[i+1], cases[i+2], 1))
		t.name = "[cases[i]]m"
		t.layer = cases[i+3]
		var/obj/scr/b = new
		b.name = "[cases[i]]s"
		b.layer = cases[i+4]
		b.screen_loc = "[cases[i+1]],[cases[i+2]]"
		client.screen += b
mob/verb/examination(atom/A as mob|obj|turf in view(src))
	set name = "Examine"
	world.log << "EXAMINE [A]"
