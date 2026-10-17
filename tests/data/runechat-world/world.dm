// RUNECHAT: vg's speech bubble and runechat. An NPC beside the player speaks every 3 s; each line is:
//   * a speech bubble: image(icon, loc = NPC, state, MOB_LAYER+1), plane 16, RESET_COLOR, in client.images for 3 s;
//   * a runechat message: MeasureText, then image(loc = NPC, layer = 7) with no icon, plane 25, alpha 0, maptext 96 x mheight
//     at maptext_x -32, pixel_y = 0.95 * 32, in client.images; animate(alpha 255, 2); at 4.3 s animate(alpha 0, 7, ANIMATION_PARALLEL);
//     at 5 s removed from client.images.
// The markup is vg's for a mob speaker at distance 1 with outlines on, laid out against vg's map-control style (world.dmf).
world
	name = "Fugue Runechat"
	view = 7
	turf = /turf/floor
	mob = /mob/player
	fps = 10

turf/floor
	icon = 'turf.dmi'
	icon_state = "floor"

var/mob/npc/NPC

mob/npc
	icon = 'mob.dmi'
	icon_state = "lobby"

mob/player
	icon = 'mob.dmi'
	icon_state = "lobby"
	Login()
		..()
		loc = locate(12, 10, 1)
		NPC = new /mob/npc(locate(13, 10, 1))
		spawn(0) talkloop()

mob/player/proc/talkloop()
	var/n = 0
	while(client)
		sleep(30)
		n++
		world << "SAY [n]"
		bubble(NPC)
		spawn(0) chat(NPC, "Hello there [n]")

mob/player/proc/bubble(atom/speaker)
	var/image/I = image('turf.dmi', speaker, "wall", MOB_LAYER + 1)
	I.plane = 16
	I.appearance_flags = RESET_COLOR
	spawn(0)
		client.images += I
		sleep(30)
		if(client) client.images -= I

mob/player/proc/chat(atom/speaker, text)
	var/complete_text = "<span class='center maptext black_outline' style='color: #ffd700;'>[text]</span>"
	var/m = client.MeasureText(complete_text, null, 96)
	var/mheight = text2num(copytext(m, findtextEx(m, "x") + 1))
	world << "MEASURE [m] -> [mheight]"
	var/image/message = image(loc = speaker, layer = 7)
	message.plane = 25
	message.appearance_flags = RESET_COLOR|RESET_TRANSFORM|NO_CLIENT_COLOR|RESET_ALPHA|PIXEL_SCALE|KEEP_APART
	message.alpha = 0
	message.pixel_y = bound_height * 0.95
	message.maptext_width = 96
	message.maptext_height = mheight
	message.maptext_x = (96 - bound_width) * -0.5
	message.maptext = complete_text
	client.images += message
	animate(message, alpha = 255, time = 2)
	sleep(43)
	animate(message, alpha = 0, time = 7, flags = ANIMATION_PARALLEL)
	sleep(7)
	if(client) client.images -= message
