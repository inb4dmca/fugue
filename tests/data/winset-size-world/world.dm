// Probe: what a runtime winset of a window's or a control's size does, and what winget reads back after it.
// A click on the map runs the steps for that client; each step logs the wingets a second after its winset.
world
	name = "Winset Size Probe"
	view = 3
	turf = /turf/floor
	mob = /mob/player
	maxx = 15
	maxy = 15
proc/note(t)
	text2file("[t]\n", "events.log")
turf/floor
	icon = 'floor.png'
turf/tile
	icon = 'tile.png'
mob/player
	invisibility = 101
	Login()
		..()
		loc = locate(8,8,1)
		var/turf/T = locate(8,8,1)
		T.icon = 'tile.png'
		note("READY [key]")
client/var/ran = 0
client/Click()
	if(ran) return
	ran = 1
	spawn() steps()
client/proc/report(label)
	var/t = "[label]:"
	for(var/id in list("main", "map", "fixed", "half"))
		for(var/p in list("size", "pos"))
			t += " [id].[p]=[winget(src, id, p)]"
	t += " main.inner-size=[winget(src, "main", "inner-size")] main.outer-size=[winget(src, "main", "outer-size")]"
	t += " map.view-size=[winget(src, "map", "view-size")]"
	note(t)
client/proc/phase(label, control, params)
	winset(src, control, params)
	sleep(10)
	report(label)
client/proc/steps()
	report("start")
	phase("win288", null, "main.size=288x288")
	phase("map160", null, "map.size=160x160")
	phase("win224", null, "main.size=224x224")
	phase("win288b", null, "main.size=288x288")
	phase("children", null, "fixed.size=64x64;half.size=100x20")
	phase("map0x0", null, "map.size=0x0")
	phase("map100x0", null, "map.size=100x0")
	phase("reset", null, "main.size=224x224;map.size=224x224")
	phase("fixture", null, "main.size=288x288;map.size=288x288")
	phase("ctlwin", "main", "size=256x256")
	phase("ctlmap", "map", "size=128x128")
	note("DONE")
