// HUDOVERLAY: overlays and underlays on client.screen objects, beside a plane master unless NO_PM is defined
// (world-nopm.dme). Every parent is 'base' (blue, a 4 px transparent border) at layer 20; Click() logs what stock
// DreamSeeker sends. Icons: mkdmi.py. "late" = added 1 s after the object went on the screen.
//   row 14  c1 image overlay   c2 late image overlay   c3 icon_state overlay   c4 late icon_state overlay
//           c5 late overlay layer 21   c6 full overlay layer 1   c7 full overlay plane 1
//   row 12  c8 full overlay plane -1   c9 image underlay   c10 late underlay   c11 underlay layer 30
//           c12 overlay pixel 8,8   c13 overlay pixel_x 24 (into 13,12)   c14 overlay with its own overlay
//   row 10  c15 parent color #808080   c16 the same, overlay RESET_COLOR   c17 parent alpha 128
//           c18 the same, overlay RESET_ALPHA   c19 iconless parent with an overlay   c20 late overlay with a late overlay
//   row 6   c21 the parent itself on plane -1   c22 over a map obj on plane 1   c23 over a map obj at layer 30
world
	name = "Fugue HUD Overlay"
	view = 7
	turf = /turf/floor
	mob = /mob/player
	maxx = 30
	maxy = 30
	maxz = 1

turf/floor
	icon = 'h.dmi'
	icon_state = "floor"
turf/floor/Click(loc, ctl, params)
	world.log << "CLICK turf [x],[y] [params]"

obj/scr
	icon = 'h.dmi'
	icon_state = "base"
	layer = 20
obj/scr/Click(loc, ctl, params)
	world.log << "CLICK [name] [params]"

obj/master
	appearance_flags = PLANE_MASTER
	screen_loc = "CENTER"
	plane = 10
	mouse_opacity = 0

mob/player
	icon = null

proc/img(st)
	return image('h.dmi', icon_state = st)

mob/player/proc/mk(nm, loc, list/ov, list/un)
	var/obj/scr/o = new
	o.name = nm
	o.screen_loc = loc
	for(var/x in ov) o.overlays += x
	for(var/x in un) o.underlays += x
	client.screen += o
	return o

mob/player/Login()
	..()
	loc = locate(15, 15, 1)
#ifndef NO_PM
	client.screen += new /obj/master
#endif
	var/image/I
	mk("c1", "2,14", list(img("ov")))
	var/obj/scr/c2 = mk("c2", "4,14")
	mk("c3", "6,14", list("ov"))
	var/obj/scr/c4 = mk("c4", "8,14")
	var/obj/scr/c5 = mk("c5", "10,14")
	I = img("full")
	I.layer = 1
	mk("c6", "12,14", list(I))
	I = img("full")
	I.plane = 1
	mk("c7", "14,14", list(I))
	I = img("full")
	I.plane = -1
	mk("c8", "2,12", list(I))
	mk("c9", "4,12", null, list(img("un")))
	var/obj/scr/c10 = mk("c10", "6,12")
	I = img("ov")
	I.layer = 30
	mk("c11", "8,12", null, list(I))
	I = img("ov")
	I.pixel_x = 8
	I.pixel_y = 8
	mk("c12", "10,12", list(I))
	I = img("ov")
	I.pixel_x = 24
	mk("c13", "12,12", list(I))
	I = img("ov")
	I.overlays += img("ov2")
	mk("c14", "14,12", list(I))
	var/obj/scr/c15 = mk("c15", "2,10", list(img("ov")))
	c15.color = rgb(128, 128, 128)
	I = img("ov")
	I.appearance_flags = RESET_COLOR
	var/obj/scr/c16 = mk("c16", "4,10", list(I))
	c16.color = rgb(128, 128, 128)
	var/obj/scr/c17 = mk("c17", "6,10", list(img("ov")))
	c17.alpha = 128
	I = img("ov")
	I.appearance_flags = RESET_ALPHA
	var/obj/scr/c18 = mk("c18", "8,10", list(I))
	c18.alpha = 128
	var/obj/scr/c19 = new
	c19.name = "c19"
	c19.icon = null
	c19.screen_loc = "10,10"
	c19.overlays += img("ov")
	client.screen += c19
	var/obj/scr/c20 = mk("c20", "12,10")
	var/obj/scr/c21 = mk("c21", "2,6")
	c21.plane = -1
	mk("c22", "4,6")
	var/obj/m = new(locate(11, 13, 1))
	m.icon = 'h.dmi'
	m.icon_state = "full"
	m.plane = 1
	mk("c23", "6,6")
	m = new(locate(13, 13, 1))
	m.icon = 'h.dmi'
	m.icon_state = "full"
	m.layer = 30
	spawn(10)
		c2.overlays += img("ov")
		c4.overlays += "ov"
		I = img("ov")
		I.layer = 21
		c5.overlays += I
		c10.underlays += img("un")
		c20.overlays += img("ov")
		sleep(5)
		I = img("ov2")
		I.layer = 22
		c20.overlays += I
		world.log << "SCENE DONE"
