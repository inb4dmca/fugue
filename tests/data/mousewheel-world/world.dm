// MouseWheel contrast world (tests/mousewheel_test.cpp). Pick ONE declaration below and recompile:
//   CLIENT_WHEEL  client/MouseWheel, which steps client.view as vg's buildmode.dm does (0x003b second word 0x02)
//   ATOM_WHEEL    /atom/MouseWheel only (no 0x003b bit; every appearance carries flag 0x100000)
//   OBJ_WHEEL     /obj/thing/MouseWheel only (only the thing's appearance carries it)
//   OBJ_MOVE      /obj/thing/MouseMove only (the same appearance flag; stock still sends it wheel notches)
//   none          no Mouse proc: stock sends no wheel frame at all
#define CLIENT_WHEEL

world
	fps = 10
	view = 6
	icon_size = 32
	mob = /mob/player
	turf = /turf/cell
	maxx = 40
	maxy = 40
	maxz = 1

turf/cell
	icon = 'grid.dmi'
	icon_state = "a"

obj/thing
	name = "thing"
	icon = 'grid.dmi'
	icon_state = "hud"

obj/hud
	name = "hudthing"
	icon = 'grid.dmi'
	icon_state = "hud2"
	screen_loc = "1,1"

mob/player
	icon = null

world/New()
	..()
	for(var/x = 1 to world.maxx)
		for(var/y = 1 to world.maxy)
			var/turf/T = locate(x, y, 1)
			if((x + y) % 2)
				T.icon_state = "b"
	new /obj/thing(locate(21, 20, 1))
	world.log << "WORLD READY"

mob/player/Login()
	..()
	loc = locate(20, 20, 1)
	client.screen += new /obj/hud
	for(var/i = 1 to 200)
		src << "line [i] of filler so the output pane can scroll"

atom/Click(location, control, params)
	world.log << "CLICK\t[src]\t[location]\t[control]\t[params]"

client/verb/setview(v as text)
	view = v
	world.log << "SETVIEW\t[view]"

#ifdef CLIENT_WHEEL
client/MouseWheel(object, delta_x, delta_y, location, control, params)
	world.log << "CWHEEL\t[object]\t[delta_x]\t[delta_y]\t[location]\t[control]\t[params]\tview=[view]"
	if(delta_y > 0)
		view = max(1, (isnum(view) ? view : 7) - 1)
	else
		view = (isnum(view) ? view : 7) + 1
	..()
#endif
#ifdef ATOM_WHEEL
atom/MouseWheel(delta_x, delta_y, location, control, params)
	world.log << "AWHEEL\t[src]\t[delta_x]\t[delta_y]\t[location]\t[control]\t[params]"
#endif
#ifdef OBJ_WHEEL
obj/thing/MouseWheel(delta_x, delta_y, location, control, params)
	world.log << "OWHEEL\t[src]\t[delta_x]\t[delta_y]\t[location]\t[control]\t[params]"
#endif
#ifdef OBJ_MOVE
obj/thing/MouseMove(location, control, params)
	world.log << "OMOVE\t[src]"
#endif
