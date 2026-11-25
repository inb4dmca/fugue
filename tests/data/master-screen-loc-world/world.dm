// Probe: does a filtered plane master act on its plane without a usable screen_loc?
world
	name = "Master Loc Probe"
	view = 3
	turf = /turf/floor
	mob = /mob/player
	maxx = 7
	maxy = 7
	fps = 10
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
	if(!locate(/obj/part) in world)
		new /obj/part(locate(4,4,1))
	note("READY login")
	spawn() phases()
mob/player/proc/phases()
	var/phase = ""
	var/obj/master/M
	while(client)
		var/p = file2text("phase.txt")
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
			note("READY [phase]")
		sleep(1)
