// A mob moving like vg's observer: appearance_flags without LONG_GLIDE, glide_size 32 / ceil(0.75 / tick_lag), one
// step per 3 ticks. Four diagonal steps, then three cardinal ones.
world
	tick_lag = 0.33
	view = 7
	icon_size = 32
	maxx = 40
	maxy = 40
	movement_mode = PIXEL_MOVEMENT_MODE
	turf = /turf
	mob = /mob/walker

atom
	appearance_flags = TILE_BOUND|LONG_GLIDE|TILE_MOVER

mob/walker
	appearance_flags = KEEP_TOGETHER

client/New()
	. = ..()
	mob.loc = locate(20, 20, 1)
	spawn(10) walk_steps(mob)

proc/walk_steps(mob/M)
	M.glide_size = 32 / max(ceil(0.75 / world.tick_lag), 1)
	for(var/i = 1, i <= 7, i++)
		var/d = i <= 4 ? NORTHEAST : EAST
		M.loc = get_step(M, d)
		M.dir = d
		sleep(4 * world.tick_lag)   // stock DD steps it every 3 ticks (99 ms)
