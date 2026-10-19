// Fugue Verb Override: a /mob verb overridden on a subtype keeps its name.
world
	name = "Fugue Verb Override"
	mob = /mob/living/carbon/human
	view = 3
	maxx = 5
	maxy = 5
	maxz = 1

turf
	icon = null
	name = "floor"

/mob/verb/say_verb(message as text)
	set name = "Say"
	set category = "IC"
	world.log << "VERB Say [type] [message]"

/mob/verb/whisper(message as text)
	set name = "Whisper"
	set category = "IC"
	world.log << "VERB Whisper-mob [type] [message]"

/mob/verb/me_verb(message as text)
	set name = "Me"
	set category = "IC"
	world.log << "VERB Me [type] [message]"

/mob/living/whisper(message as text)
	world.log << "VERB Whisper-living [type] [message]"

/mob/living/silicon/robot/whisper(message as text)
	world.log << "VERB Whisper-robot [type] [message]"

/mob/living/silicon/ai/me_verb(message as text)
	world.log << "VERB Me-ai [type] [message]"

/mob/proc/become(path)
	var/mob/M = new path(locate(2, 2, 1))
	world.log << "BECOME [M.type]"
	client.mob = M

/mob/verb/become_ai()
	become(/mob/living/silicon/ai)
/mob/verb/become_robot()
	become(/mob/living/silicon/robot)
/mob/verb/become_plain()
	become(/mob)
/mob/verb/become_human()
	become(/mob/living/carbon/human)

/mob/living/carbon/human
/mob/living/silicon/ai
/mob/living/silicon/robot

/mob/Login()
	if(!loc) loc = locate(3, 3, 1)
	..()

/mob/living/carbon/human/verb/human_only()
	set name = "Human Only"
	world.log << "VERB Human-only [type]"

/mob/verb/poke(n as num)
	set name = "Poke"
	world.log << "VERB Poke-mob [type] [n]"

/mob/living/poke(t as text)
	world.log << "VERB Poke-living [type] [t]"

/obj/verb/use_it()
	set name = "Use It"
	set src in view()
	world.log << "VERB UseIt-obj [type] [name]"

/obj/lamp/use_it()
	world.log << "VERB UseIt-lamp [type] [name]"

/obj/lamp
	name = "alamp"
/obj/desk
	name = "zdesk"

/world/New()
	..()
	new /obj/desk(locate(4, 3, 1))
	new /obj/lamp(locate(3, 4, 1))
	var/obj/desk/D = new(locate(2, 3, 1))
	D.name = "mdesk"
