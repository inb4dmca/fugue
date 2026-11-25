// Probe: does a filtered plane master act on its plane without a usable screen_loc?
// Level 1 is the original 7x7 room (the tiles past 7 have no icon); level 2 is a 15x15 room of striped columns with a
// few objects, for phases that end in "|wide": the eye and the part stand at its centre, 8,8, so turfs lie past the view.
world
	name = "Master Loc Probe"
	view = 3
	turf = /turf/floor
	mob = /mob/player
	maxx = 15
	maxy = 15
	maxz = 2
	fps = 10
world/New()
	..()
	for(var/turf/T in block(locate(1,1,1), locate(15,15,1)))
		if(T.x > 7 || T.y > 7) T.icon = null
	for(var/turf/T in block(locate(1,1,2), locate(15,15,2)))
		// columns told apart by their icon, three by three
		T.icon_state = list("floor", "mob", "rock")[T.x % 3 + 1]
	// a turf and an object on the default plane coloured by a colour matrix (a constant colour), in the view
	var/turf/C = locate(10,10,2)
	C.color = list(0,0,0, 0,0,0, 0,0,0, 1,0,1)
	var/obj/K = new(locate(10,6,2)); K.icon = 'z.dmi'; K.icon_state = "nst"; K.color = list(0,0,0, 0,0,0, 0,0,0, 1,0.5,0)
proc/note(t)
	text2file("[t]\n", "events.log")
turf/floor
	icon = 'z.dmi'
	icon_state = "floor"
mob/player
	invisibility = 101
obj/part
	icon = 'z.dmi'
	icon_state = "st"
	plane = 1
obj/master
	plane = 1
mob/player/Login()
	..()
	loc = locate(4,4,1)
	if(!locate(/obj/part) in locate(4,4,1))
		new /obj/part(locate(4,4,1))
		new /obj/part(locate(8,8,2))
		// on level 2, objects on the default plane in the view (6,8) and past it (12,8 and 4,8), and one on plane 1
		// past it (12,9)
		var/obj/O = new(locate(6,8,2)); O.icon = 'z.dmi'; O.icon_state = "nst"
		O = new(locate(12,8,2)); O.icon = 'z.dmi'; O.icon_state = "nst"
		O = new(locate(4,8,2)); O.icon = 'z.dmi'; O.icon_state = "rock"
		O = new(locate(12,9,2)); O.icon = 'z.dmi'; O.icon_state = "img"; O.plane = 1
		// and, moved half a tile right, on the default plane and on plane 1 at the view's left edge (5,9 and 5,10)
		O = new(locate(5,9,2)); O.icon = 'z.dmi'; O.icon_state = "nst"; O.pixel_x = 16
		O = new(locate(5,10,2)); O.icon = 'z.dmi'; O.icon_state = "nst"; O.pixel_x = 16; O.plane = 1
	note("READY login")
	spawn() phases()
// The phase comes from phase.txt (collect.py), or once from world.params "case" (stockprobe.py pair), where "_" stands
// for "|" and "~" for ":" so that the case name can be a file name.
mob/player/proc/phases()
	var/phase = ""
	var/obj/master/M
	var/given = world.params["case"]
	if(given) given = replacetext(replacetext(given, "_", "|"), "~", ":")
	while(client)
		var/p = given ? given : file2text("phase.txt")
		if(p && p != phase)
			phase = p
			if(M) client.screen -= M
			M = null
			if(p != "none")
				M = new
				M.appearance_flags = PLANE_MASTER
				var/list/w = splittext(p, "|")
				var/loc = w[1]
				if(loc == "null") M.screen_loc = null
				else if(loc == "empty") M.screen_loc = ""
				else M.screen_loc = loc
				var/fx = w[2]
				var/turf/where = (w.len >= 3 && w[3] == "wide") ? locate(8,8,2) : locate(4,4,1)
				if(src.loc != where) src.loc = where
				if(fx == "outline") M.filters = filter(type="outline", size=1, color="#ffffff")
				if(fx == "blur") M.filters = filter(type="blur", size=2)
				if(fx == "shadow") M.filters = filter(type="drop_shadow", x=4, y=-4, size=0, offset=0, color="#00ff00")
				if(fx == "alpha0") M.alpha = 0
				if(fx == "green") M.color = "#00ff00"
				if(fx == "kt") M.appearance_flags |= KEEP_TOGETHER
				if(fx == "kt-alpha0")
					M.appearance_flags |= KEEP_TOGETHER
					M.alpha = 0
				if(fx == "ov")
					var/image/I = image('z.dmi', icon_state="img")
					M.overlays += I
				if(fx == "ov-outline")
					var/image/I = image('z.dmi', icon_state="img")
					M.overlays += I
					M.filters = filter(type="outline", size=1, color="#ffffff")
				if(fx == "obj")
					M.appearance_flags = 0
					M.icon = 'z.dmi'
					M.icon_state = "img"
				if(fx == "rt")
					M.render_target = "*cap"
					var/obj/R = new
					R.render_source = "*cap"
					R.screen_loc = "2,2"
					R.plane = 5
					R.tag = "relay"
					client.screen += R
				client.screen += M
			else
				for(var/obj/O in client.screen) client.screen -= O
				if(src.loc != locate(4,4,1)) src.loc = locate(4,4,1)
			note("READY [phase]")
		sleep(1)
