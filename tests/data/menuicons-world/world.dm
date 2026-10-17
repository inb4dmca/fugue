// Right-click menu icon contrast world: one atom per tile, each isolating one way its menu icon can differ from its plain sprite.
// The player sits iconless at (16,16); view 7.
world
	name = "menuicons"
	fps = 10
	view = 7
	icon_size = 32
	mob = /mob/player
	turf = /turf/floor
	maxx = 31
	maxy = 31
	maxz = 1

turf/floor
	name = "floor"
	icon = 'scene.dmi'
	icon_state = "floor"

mob/player
	icon = null
	name = "player"
	Login()
		..()
		loc = locate(16,16,1)
		var/obj/screen/S = new
		client.screen += S
		var/obj/held/H = new(src)
		client.screen += H

obj
	verb/poke()
		set src in view()
		world.log << "POKE [name]"

obj/screen
	name = "hudthing"
	icon = 'scene.dmi'
	icon_state = "hud"
	screen_loc = "1,1"

// An item the player CARRIES and sees on the HUD (vg's in-hand items): a client.screen atom whose verb is in scope.
obj/held
	name = "held"
	icon = 'scene.dmi'
	icon_state = "left"
	color = "#0000ff"
	screen_loc = "2,1"
	verb/squeeze()
		set src in usr
		world.log << "SQUEEZE [name]"

obj/plain
	name = "plain"
	icon = 'scene.dmi'
	icon_state = "solid"

obj/matrix
	name = "matrix"
	icon = 'scene.dmi'
	icon_state = "solid"
	New()
		..()
		color = list(0,0,1, 0,1,0, 1,0,0)

obj/tinted
	name = "tinted"
	icon = 'scene.dmi'
	icon_state = "solid"
	color = "#00ff00"

obj/offset
	name = "offset"
	icon = 'scene.dmi'
	icon_state = "dot8"
	pixel_x = 10
	pixel_y = 24

obj/nullicon
	name = "nullicon"
	icon = null
	New()
		..()
		overlays += image('scene.dmi', "dot8")

obj/turned
	name = "turned"
	icon = 'scene.dmi'
	icon_state = "left"
	New()
		..()
		transform = turn(matrix(), 90)

obj/faded
	name = "faded"
	icon = 'scene.dmi'
	icon_state = "solid"
	alpha = 60

obj/overlaid
	name = "overlaid"
	icon = 'scene.dmi'
	icon_state = "left"
	New()
		..()
		var/image/I = image('scene.dmi', "dot8")
		I.pixel_x = 8
		overlays += I

obj/ovcolor
	name = "ovcolor"
	icon = 'scene.dmi'
	icon_state = "left"
	New()
		..()
		var/image/I = image('scene.dmi', "dot8")
		I.color = "#ff0000"
		overlays += I

obj/parentcolor
	name = "parentcolor"
	icon = 'scene.dmi'
	icon_state = "left"
	color = "#0000ff"
	New()
		..()
		overlays += image('scene.dmi', "dot8")

world/New()
	..()
	new /obj/plain(locate(15,17,1))
	new /obj/matrix(locate(16,17,1))
	new /obj/tinted(locate(17,17,1))
	new /obj/offset(locate(15,16,1))
	new /obj/nullicon(locate(17,16,1))
	new /obj/turned(locate(15,15,1))
	new /obj/faded(locate(16,15,1))
	new /obj/overlaid(locate(17,15,1))
	new /obj/ovcolor(locate(14,17,1))
	new /obj/parentcolor(locate(14,15,1))
