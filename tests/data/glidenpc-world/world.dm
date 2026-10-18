// Two non-player mobs glide while the player stands still, in vg's movement mode. Red: diagonal at vg observer speed
// (no LONG_GLIDE, glide_size 32/3, a step every 3 ticks). Blue: cardinal, glide_size 4, slower than its step rate.
// Six steps out, a rest, six steps back.
var/global/delay = 0.75
var/global/legs = 6
var/global/blueglide = 4

world
	tick_lag = 0.33
	view = "15x15"
	icon_size = 32
	movement_mode = PIXEL_MOVEMENT_MODE
	maxx = 40
	maxy = 40
	maxz = 1
	turf = /turf
	mob = /mob/player

atom
	appearance_flags = TILE_BOUND|LONG_GLIDE|TILE_MOVER

mob/npc
	appearance_flags = KEEP_TOGETHER

var/global/mob/npc/R
var/global/mob/npc/B
var/global/started = 0

mob/player/Login()
	..()
	loc = locate(20, 20, 1)
	if(!started)
		started = 1
		R = new(locate(14, 14, 1))
		B = new(locate(14, 25, 1))
		spawn(10) walkloop()

proc/glide_for(dl)
	return 32 / max(ceil(dl / world.tick_lag), 1)

proc/walkloop()
	for(var/leg = 0, leg < 2, leg++)
		var/n = 0
		var/next = world.time
		while(n < legs)
			if(world.time < next)
				sleep(world.tick_lag)
				continue
			next = world.time + delay
			R.glide_size = glide_for(delay)
			B.glide_size = blueglide
			R.loc = get_step(R, leg ? SOUTHWEST : NORTHEAST)
			B.loc = get_step(B, leg ? WEST : EAST)
			n++
		sleep(15)
