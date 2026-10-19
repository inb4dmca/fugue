// Fugue Verb Cell: which of several same-named srcs on ONE cell a typed verb runs on, as atoms arrive, leave, come back,
// move within the cell or change appearance. The player stands at 7,7; parked atoms wait out of sight at x 18.
// Every scene's srcs share a cell, so distance ties and the cell's own order decides.
//   Drum   9,9:  drum h at boot; drum l (lower ref) arrives at T+1.
//   Knock  5,9:  knock a then knock b at boot; knock a leaves at T+1 and comes back at T+2.
//   Chime  9,5:  chime c at boot; chime x arrives at T+1, chime y (lowest ref) at T+2. Peal: only x and c carry it.
//   Woof   5,5:  npc m1 (a mob) at boot; obj w arrives at T+1. Bark: npc m2 (lower mob ref) arrives at T+2 onto m1.
//   Toll   7,10: toll a then toll b at boot; toll a's appearance changes at T+3.
//   Rasp   7,4:  rasp a then rasp b at boot; rasp a steps within the cell (step_x 4) at T+3.
//   Scrape 4,7:  scrape a then scrape b at boot; scrape a's loc is set to its own turf at T+3.
//   Beep   10,7: beep o at boot; a parked dummy (the lowest ref) is deleted at T+1, beep n is made on 10,7 at T+2.
/obj/druml/verb/drum_l()
	set name = "Drum"
	set src = view(5)
	world.log << "VERB Drum l [x],[y] \ref[src]"
/obj/druml
	name = "drumer"
/obj/drumh/verb/drum_h()
	set name = "Drum"
	set src = view(5)
	world.log << "VERB Drum h [x],[y] \ref[src]"
/obj/drumh
	name = "drumer"
/obj/knocka/verb/knock_a()
	set name = "Knock"
	set src = view(5)
	world.log << "VERB Knock a [x],[y] \ref[src]"
/obj/knocka
	name = "knocker"
/obj/knockb/verb/knock_b()
	set name = "Knock"
	set src = view(5)
	world.log << "VERB Knock b [x],[y] \ref[src]"
/obj/knockb
	name = "knocker"
/obj/chimey/verb/chime_y()
	set name = "Chime"
	set src = view(5)
	world.log << "VERB Chime y [x],[y] \ref[src]"
/obj/chimey
	name = "chimeer"
/obj/chimex/verb/chime_x()
	set name = "Chime"
	set src = view(5)
	world.log << "VERB Chime x [x],[y] \ref[src]"
/obj/chimex
	name = "chimeer"
/obj/chimec/verb/chime_c()
	set name = "Chime"
	set src = view(5)
	world.log << "VERB Chime c [x],[y] \ref[src]"
/obj/chimec
	name = "chimeer"
/obj/chimex/verb/peal_x()
	set name = "Peal"
	set src = view(5)
	world.log << "VERB Peal x [x],[y] \ref[src]"
/obj/chimec/verb/peal_c()
	set name = "Peal"
	set src = view(5)
	world.log << "VERB Peal c [x],[y] \ref[src]"
/obj/woofw/verb/woof_w()
	set name = "Woof"
	set src = view(5)
	world.log << "VERB Woof w [x],[y] \ref[src]"
/obj/woofw
	name = "woofer"
/obj/tolla/verb/toll_a()
	set name = "Toll"
	set src = view(5)
	world.log << "VERB Toll a [x],[y] \ref[src]"
/obj/tolla
	name = "toller"
/obj/tollb/verb/toll_b()
	set name = "Toll"
	set src = view(5)
	world.log << "VERB Toll b [x],[y] \ref[src]"
/obj/tollb
	name = "toller"
/obj/raspa/verb/rasp_a()
	set name = "Rasp"
	set src = view(5)
	world.log << "VERB Rasp a [x],[y] \ref[src]"
/obj/raspa
	name = "rasper"
/obj/raspb/verb/rasp_b()
	set name = "Rasp"
	set src = view(5)
	world.log << "VERB Rasp b [x],[y] \ref[src]"
/obj/raspb
	name = "rasper"
/obj/scrapea/verb/scrape_a()
	set name = "Scrape"
	set src = view(5)
	world.log << "VERB Scrape a [x],[y] \ref[src]"
/obj/scrapea
	name = "scrapeer"
/obj/scrapeb/verb/scrape_b()
	set name = "Scrape"
	set src = view(5)
	world.log << "VERB Scrape b [x],[y] \ref[src]"
/obj/scrapeb
	name = "scrapeer"
/obj/beepo/verb/beep_o()
	set name = "Beep"
	set src = view(5)
	world.log << "VERB Beep o [x],[y] \ref[src]"
/obj/beepo
	name = "beeper"
/obj/beepn/verb/beep_n()
	set name = "Beep"
	set src = view(5)
	world.log << "VERB Beep n [x],[y] \ref[src]"
/obj/beepn
	name = "beeper"
/mob/npc
	name = "npc"
/mob/npc/m1/verb/woof_m()
	set name = "Woof"
	set src = view(5)
	world.log << "VERB Woof m1 [x],[y] \ref[src]"
/mob/npc/m1/verb/bark_1()
	set name = "Bark"
	set src = view(5)
	world.log << "VERB Bark m1 [x],[y] \ref[src]"
/mob/npc/m2/verb/bark_2()
	set name = "Bark"
	set src = view(5)
	world.log << "VERB Bark m2 [x],[y] \ref[src]"
/obj/dummy
	name = "dummy"

var/obj/dummy/dummy
var/obj/druml/dl
var/obj/knocka/ka
var/obj/chimex/cx
var/obj/chimey/cy
var/obj/woofw/ww
var/mob/npc/m2/m2
var/obj/tolla/ta
var/obj/raspa/ra
var/obj/scrapea/sa
var/started = 0
world
	name = "Fugue Verb Cell"
	mob = /mob/living/carbon/human
	view = 5
	maxx = 20
	maxy = 20
	maxz = 1
turf
	icon = null
	name = "floor"
area
	name = "room"
/mob/living/carbon/human
/mob/living/carbon/human/Login()
	if(!loc) loc = locate(7, 7, 1)
	..()
	if(started) return
	started = 1
	spawn(10)
		dl.loc = locate(9, 9, 1)
		ka.loc = locate(18, 10, 1)
		cx.loc = locate(9, 5, 1)
		ww.loc = locate(5, 5, 1)
		del(dummy)
		world.log << "T1 dl \ref[dl] ka \ref[ka] cx \ref[cx] ww \ref[ww]"
		sleep(10)
		ka.loc = locate(5, 9, 1)
		cy.loc = locate(9, 5, 1)
		m2.loc = locate(5, 5, 1)
		var/obj/n = new /obj/beepn(locate(10, 7, 1))
		world.log << "T2 ka \ref[ka] cy \ref[cy] m2 \ref[m2] beep n \ref[n]"
		sleep(10)
		ta.desc = "changed"
		ra.step_x = 4
		sa.loc = sa.loc
		world.log << "T3 ta \ref[ta] ra \ref[ra] [ra.step_x] sa \ref[sa] [sa.x],[sa.y]"
/world/New()
	..()
	dummy = new /obj/dummy(locate(18, 1, 1))
	cy = new /obj/chimey(locate(18, 2, 1))
	dl = new /obj/druml(locate(18, 3, 1))
	new /obj/drumh(locate(9, 9, 1))
	ka = new /obj/knocka(locate(5, 9, 1))
	var/obj/kb = new /obj/knockb(locate(5, 9, 1))
	cx = new /obj/chimex(locate(18, 4, 1))
	var/obj/cc = new /obj/chimec(locate(9, 5, 1))
	m2 = new /mob/npc/m2(locate(18, 5, 1))
	var/mob/m1 = new /mob/npc/m1(locate(5, 5, 1))
	ww = new /obj/woofw(locate(18, 6, 1))
	ta = new /obj/tolla(locate(7, 10, 1))
	var/obj/tb = new /obj/tollb(locate(7, 10, 1))
	ra = new /obj/raspa(locate(7, 4, 1))
	var/obj/rb = new /obj/raspb(locate(7, 4, 1))
	sa = new /obj/scrapea(locate(4, 7, 1))
	var/obj/sb = new /obj/scrapeb(locate(4, 7, 1))
	var/obj/bo = new /obj/beepo(locate(10, 7, 1))
	world.log << "BOOT dummy \ref[dummy] cy \ref[cy] dl \ref[dl] ka \ref[ka] kb \ref[kb] cx \ref[cx] cc \ref[cc] m2 \ref[m2] m1 \ref[m1] ww \ref[ww] ta \ref[ta] tb \ref[tb] ra \ref[ra] rb \ref[rb] sa \ref[sa] sb \ref[sb] bo \ref[bo]"
