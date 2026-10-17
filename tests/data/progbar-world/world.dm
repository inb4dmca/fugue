// PROGBAR: vg's do_after progress bar. Each tick the bar is an image on the target: pixel_z 32, plane 24 (HUD_PLANE),
// layer 3 (HUD_ABOVE_ITEM_LAYER), appearance_flags RESET_COLOR | RESET_TRANSFORM, icon_state prog_bar_N stepped each tick,
// in client.images while it runs, then removed with progbar.loc = null.
// vg's lighting plane master (plane 15, BLEND_MULTIPLY) and a full-view BLEND_ADD white quad sit on client.screen, so the bar is
// drawn on vg's plane stack. Targets: the player's own mob, then a coloured crate beside it. Icons: mkdmi.py. Skin: world.dmf (stored under res/).
world
	name = "Fugue Progbar"
	view = 4
	turf = /turf/floor
	mob = /mob/player
	fps = 10
	maxx = 10
	maxy = 10
	maxz = 1

turf/floor
	icon = 'thing.dmi'
	icon_state = "floor"

obj/crate
	icon = 'thing.dmi'
	icon_state = "crate"
	color = "#ff8000"

obj/screen/lightmaster
	icon = 'thing.dmi'
	screen_loc = "CENTER"
	plane = 15
	blend_mode = BLEND_MULTIPLY
	appearance_flags = NO_CLIENT_COLOR | PLANE_MASTER | RESET_TRANSFORM | RESET_COLOR | RESET_ALPHA
	color = list(null,null,null,"#0000","#000F")
	mouse_opacity = 0

obj/screen/lit
	icon = 'thing.dmi'
	icon_state = "white"
	screen_loc = "SOUTHWEST to NORTHEAST"
	plane = 15
	blend_mode = BLEND_ADD
	mouse_opacity = 0

obj/screen/hudthing
	icon = 'thing.dmi'
	icon_state = "hud"
	screen_loc = "WEST,SOUTH"
	plane = 24
	layer = 1

var/obj/crate/CRATE

mob/player
	icon = 'thing.dmi'
	icon_state = "mob"
	Login()
		..()
		loc = locate(5, 5, 1)
		if(!CRATE) CRATE = new /obj/crate(locate(6, 5, 1))
		client.screen += new /obj/screen/lightmaster
		client.screen += new /obj/screen/lit
		client.screen += new /obj/screen/hudthing
		spawn(0) cycle()

mob/player/proc/cycle()
	var/n = 0
	while(client)
		sleep(30)
		n++
		world << "CYCLE [n] self"
		do_after(src, src, 100)
		world << "CYCLE [n] crate"
		do_after(src, CRATE, 100)

/proc/do_after(var/mob/user, var/atom/target, var/delay, var/numticks = 10)
	var/delayfraction = round(delay/numticks)
	var/image/progbar
	if(user && user.client && target)
		progbar = image("icon" = 'doafter.dmi', "loc" = target, "icon_state" = "prog_bar_0")
		progbar.pixel_z = 32
		progbar.plane = 24
		progbar.layer = 3
		progbar.appearance_flags = RESET_COLOR | RESET_TRANSFORM
	for (var/i = 1 to numticks)
		if(user && user.client && target)
			progbar.icon_state = "prog_bar_[round(((i / numticks) * 100), 10)]"
			user.client.images |= progbar
			world << "STEP [i] [progbar.icon_state]"
		sleep(delayfraction)
	if(user && user.client)
		user.client.images -= progbar
	if(progbar)
		progbar.loc = null
	return 1
