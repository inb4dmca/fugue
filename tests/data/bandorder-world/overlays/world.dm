// Fugue Band Overlays: overlays in another layer band than their atom, plus combined band thresholds.
world
	name = "Fugue Band Overlays"
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

// name, x, y, kind, layer, overlay layer (0 = none), kind2, layer2, overlay layer2
var/list/cases = list(
	"A", 2, 2, "m", 3, 5001, "s", 1, 0,
	"B", 4, 2, "s", 1, 5001, "m", 5002, 0,
	"C", 6, 2, "m", 25000, 0, "s", 20001, 0,
	"D", 8, 2, "m", 45001, 0, "s", 20001, 0,
	"E", 10, 2, "m", 15000, 0, "s", 5000, 0,
	"F", 12, 2, "s", 1, 20001, "", 0, 0,
	"G", 14, 2, "m", 20001, 3, "", 0, 0,
	"H", 2, 4, "m", 1, 20001, "s", 1, 0,
	"I", 4, 4, "s", 20001, 3, "m", 1, 0)

client/Click(object)
	world.log << "CLICK [object]"
	..()

mob/player/proc/mk(nm, kind, x, y, ly, ov)
	var/atom/movable/A
	if(kind == "m")
		A = new /obj/thing(locate(x, y, 1))
	else
		A = new /obj/scr
		A.screen_loc = "[x],[y]"
	A.name = nm
	A.layer = ly
	if(ov)
		var/image/I = image('thing.dmi', icon_state = "bar2")
		I.layer = ov
		A.overlays += I
	if(kind != "m") client.screen += A

mob/player/Login()
	..()
	loc = locate(8, 8, 1)
	name = "player"
	for(var/i = 1, i <= cases.len, i += 9)
		var/c = cases[i]
		mk("[c][cases[i+3]]", cases[i+3], cases[i+1], cases[i+2], cases[i+4], cases[i+5])
		if(cases[i+6] != "")
			mk("[c][cases[i+6]]", cases[i+6], cases[i+1], cases[i+2], cases[i+7], cases[i+8])
		world.log << "[c] done"

mob/verb/examination(atom/A as mob|obj|turf in view(src))
	set name = "Examine"
	world.log << "EXAMINE [A]"
