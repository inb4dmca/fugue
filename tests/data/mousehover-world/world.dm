// Hover contrast world (tests/mousehover_test.cpp). Pick ONE declaration below and recompile:
//   ATOM_HOVER    per-atom procs: hov MouseEntered/Exited, mov MouseMove, exitonly MouseExited, hovnone MouseEntered at
//                 mouse_opacity 0, the HUD objects MouseEntered/Exited (appearance flag 0x800; mov also 0x80000)
//   CLIENT_HOVER  client/MouseEntered, MouseExited and MouseMove (0x003b 0x02000000, second word 0x01)
//   CLIENT_MOVE   client/MouseMove only (the same two bits)
//   ATOM_ALL      /atom/MouseEntered (every appearance carries 0x800)
//   WHEEL_ONLY    hov MouseWheel only (0x00100804: enter/leave reports, no Move reports)
//   WALKER        an obj with MouseEntered/Exited that steps on and off (21,22) every 2 s, for a still pointer
//   none          no Mouse proc but atom/Click: stock sends no hover report at all
#define ATOM_HOVER

world
	name = "Fugue Hover"
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

obj
	icon = 'grid.dmi'
	icon_state = "hud"
obj/hov
	name = "hov"
obj/mov
	name = "mov"
obj/plain
	name = "plain"
obj/exitonly
	name = "exitonly"
obj/hovnone
	name = "hovnone"
	mouse_opacity = 0
obj/hud
	name = "hud"
	icon_state = "hud2"
	screen_loc = "1,1"
obj/hud2
	name = "hud2"
	icon_state = "hud2"
	screen_loc = "map2:1,1"

mob/player
	icon = null

world/New()
	..()
	for(var/x = 1 to world.maxx)
		for(var/y = 1 to world.maxy)
			var/turf/T = locate(x, y, 1)
			if((x + y) % 2)
				T.icon_state = "b"
	new /obj/hov(locate(22, 20, 1))
	new /obj/mov(locate(18, 20, 1))
	new /obj/exitonly(locate(20, 22, 1))
	new /obj/plain(locate(20, 18, 1))
	new /obj/hovnone(locate(24, 20, 1))
	world.log << "WORLD READY"

mob/player/Login()
	..()
	loc = locate(20, 20, 1)
	client.screen += new /obj/hud
	client.screen += new /obj/hud2

atom/Click(location, control, params)
	world.log << "CLICK\t[src]\t[location]\t[control]\t[params]\t[world.time]"

#ifdef ATOM_HOVER
obj/hov/MouseEntered(location, control, params)
	world.log << "ENTERED\t[src]\t[location]\t[control]\t[params]\t[world.time]"
obj/hov/MouseExited(location, control, params)
	world.log << "EXITED\t[src]\t[location]\t[control]\t[params]\t[world.time]"
obj/mov/MouseMove(location, control, params)
	world.log << "MOVE\t[src]\t[location]\t[control]\t[params]\t[world.time]"
obj/exitonly/MouseExited(location, control, params)
	world.log << "EXITED\t[src]\t[location]\t[control]\t[params]\t[world.time]"
obj/hovnone/MouseEntered(location, control, params)
	world.log << "ENTERED\t[src]\t[location]\t[control]\t[params]\t[world.time]"
obj/hud/MouseEntered(location, control, params)
	world.log << "ENTERED\t[src]\t[location]\t[control]\t[params]\t[world.time]"
obj/hud/MouseExited(location, control, params)
	world.log << "EXITED\t[src]\t[location]\t[control]\t[params]\t[world.time]"
obj/hud2/MouseEntered(location, control, params)
	world.log << "ENTERED\t[src]\t[location]\t[control]\t[params]\t[world.time]"
obj/hud2/MouseExited(location, control, params)
	world.log << "EXITED\t[src]\t[location]\t[control]\t[params]\t[world.time]"
#endif
#if defined(CLIENT_HOVER) || defined(CLIENT_MOVE)
client/MouseMove(object, location, control, params)
	world.log << "CMOVE\t[object]\t[location]\t[control]\t[params]\t[world.time]"
#endif
#ifdef CLIENT_HOVER
client/MouseEntered(object, location, control, params)
	world.log << "CENTERED\t[object]\t[location]\t[control]\t[params]\t[world.time]"
	..()
client/MouseExited(object, location, control, params)
	world.log << "CEXITED\t[object]\t[location]\t[control]\t[params]\t[world.time]"
	..()
#endif
#ifdef ATOM_ALL
atom/MouseEntered(location, control, params)
	world.log << "AENTERED\t[src]\t[location]\t[control]\t[params]\t[world.time]"
#endif
#ifdef WHEEL_ONLY
obj/hov/MouseWheel(delta_x, delta_y, location, control, params)
	world.log << "WHEEL\t[src]\t[world.time]"
#endif
#ifdef WALKER
obj/walker
	name = "walker"
obj/walker/MouseEntered(location, control, params)
	world.log << "WENTERED\t[src]\t[location]\t[params]\t[world.time]"
obj/walker/MouseExited(location, control, params)
	world.log << "WEXITED\t[src]\t[location]\t[params]\t[world.time]"
obj/walker/New()
	..()
	spawn(0)
		var/i = 0
		while(1)
			sleep(20)
			i++
			loc = locate(21 + (i % 2), 22, 1)
world/New()
	..()
	new /obj/walker(locate(21, 22, 1))
#endif
