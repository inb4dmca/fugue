// STATICONS - what stock DreamSeeker draws for an atom row in a Stat() panel: SS13's alt-click turf tab
// (statpanel(T.name, null, T), then statpanel(T.name, null, A) per atom), plus stat("Label:", obj).
// Each obj on the player's tile varies one thing; o_null_dirW is in nullspace (never on the map). client/Click logs its params.
// See render/scene.h (statRowIcon) and proto/enc_c2s_0002_input.h (infoClickParams).
// world.dmf is a minimal stat-panel skin. Icons:
//   python3 -I mkdmi.py .   (writes i16/i32/i64.dmi), then DreamMaker on world.dme
// With icon_size = 16 under world, every 32x32 icon draws at 16x16.
world
	name = "staticons"
	view = 3
	maxx = 5
	maxy = 5
	maxz = 1
	turf = /turf/floor
	mob = /mob/player
	fps = 10
turf/floor
	icon = 'i32.dmi'
	icon_state = "floor"
	name = "floor"
obj/t
	icon = 'i32.dmi'
	icon_state = "red"
var/obj/t/nullobj
var/statlogged = 0
mob/player
	icon = null
	name = "player"
	Login()
		..()
		loc = locate(2, 2, 1)
		client.statpanel = "Tile"
		world.log << "LOGIN [key] at [x],[y]"
	Stat()
		var/turf/T = loc
		if(!T) return
		if(!statlogged) { statlogged = 1; world.log << "STAT [T] [T.contents.len]" }
		statpanel("Tile", null, T)
		for(var/atom/A in T)
			if(A == src) continue
			statpanel("Tile", null, A)
		statpanel("Tile", null, nullobj)
		for(var/obj/t/O in T)
			if(O.name == "o_red")
				stat("Label:", O)
		stat("HP:", "[world.time > -1 ? 100 : 0]")
client/Click(object, location, control, params)
	world.log << "CLICK obj=[object] loc=[location] ctl=[control] params=[params]"
	..()
world/New()
	..()
	var/turf/T = locate(2, 2, 1)
	var/obj/t/O
	O = new(T); O.name = "o_red"
	O = new(T); O.name = "o_dirE"; O.icon_state = "dirs"; O.dir = EAST
	O = new(T); O.name = "o_ovl"; O.overlays += image('i32.dmi', "dot")
	O = new(T); O.name = "o_col"; O.icon_state = "white"; O.color = "#00ff00"
	O = new(T); O.name = "o_anim"; O.icon_state = "anim"
	O = new(T); O.name = "o_noicon"; O.icon = null
	O = new(T); O.name = "o_noicon_ovl"; O.icon = null; O.overlays += image('i32.dmi', "dot")
	O = new(T); O.name = "o_alpha"; O.alpha = 100
	O = new(T); O.name = "o_px"; O.pixel_x = 8
	O = new(T); O.name = "o_16"; O.icon = 'i16.dmi'; O.icon_state = "s"
	O = new(T); O.name = "o_64"; O.icon = 'i64.dmi'; O.icon_state = "b"
	nullobj = new(null); nullobj.name = "o_null_dirW"; nullobj.icon_state = "dirs"; nullobj.dir = WEST
