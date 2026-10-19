// Fugue Band Thresholds Two: one map obj and one screen obj per cell.
world
	name = "Fugue Band Thresholds Two"
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
	"q0", 2, 2, 15000, 4500,
	"q1", 4, 2, 15000, 4999,
	"q2", 6, 2, 15000, 4999.5,
	"q3", 8, 2, 15000, 5000,
	"q4", 10, 2, 6000, 4999,
	"q5", 12, 2, 5000.5, 4999,
	"q6", 14, 2, 5001, 5000,
	"q7", 2, 4, 25000, 21000,
	"q8", 4, 4, 25000, 22000,
	"q9", 6, 4, 25000, 23000,
	"q10", 8, 4, 25000, 23999,
	"q11", 10, 4, 25000, 24500,
	"q12", 12, 4, 25000, 24999,
	"q13", 14, 4, 25000, 25000,
	"q14", 2, 6, 24999.5, 20001,
	"q15", 4, 6, 25000.5, 20001,
	"q16", 6, 6, 26000, 24000,
	"q17", 8, 6, 30000, 24000,
	"q18", 10, 6, 25000, 20001,
	"q19", 12, 6, 25001, 24000,
	"q20", 14, 6, 25001, 24999,
	"q21", 2, 10, 5000, 4999,
	"q22", 4, 10, 20001, 1,
	"q23", 6, 10, 15000, 20000,
	"q24", 10, 10, 15000, 20000.5,
	"q25", 12, 10, 25000, 25000.5,
	"q26", 14, 10, 35000, 25000,
	"q27", 2, 12, 5001, 4000)
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

