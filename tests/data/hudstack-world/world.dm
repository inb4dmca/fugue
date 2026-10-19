// HUDSTACK: vg's equipped items on the HUD. An inventory slot (/obj/abstract/screen/inventory: plane 24, layer 1, not
// carried) and the item worn in it (carried in usr, plane 24, layer 2, the slot's screen_loc), both on client.screen.
// The player's verbs are vg's Examine (`in view(src)`), a `view()` and an `oview(1)` argument verb; the item has a
// `set src in usr` and a `set src in view(1)` verb. Every verb logs what it ran on. Icons: mkdmi.py.
//   A 3,3  slot i_clothing joins client.screen first, then the jumpsuit
//   B 5,3  the gloves join first, then slot i_gloves
//   C 7,3  slot i_shoes, empty
//   D 9,3  slot i_belt (layer 1) under an uncarried screen obj "button" (layer 2), the slot joining last
//   E 11,3 two slots at one layer, e_first joining before e_second
//   F 13:16,3  the carried item "shifted", half a tile east, alone
//   G 3,5  slot g_slot under a mouse_opacity 0 screen obj "ghost" (layer 2)
// Every cell also lies under vg's click catcher: an unnamed, blank, mouse_opacity 2 screen obj on plane -99 over the view.
// Screen objs (plane 24 unless named otherwise), with the map atoms on the same cell:
//   7,5 "under" on plane -50     9,5 "mo2" at mouse_opacity 2     11,5 named ""     1,7 named " "     3,7 named "\improper"
//   7,7 "s_p0" plane 0 layer 1, map m_p0_l5 (layer 5) and m_p0_l1 (layer 1)
//   9,7 "s_m50" plane -50, map m_m40 (plane -40) and m_m60 (plane -60)
//   11,7 "s_p24", map m_p24_l30 (plane 24 layer 30) and m_p30 (plane 30)
//   13,7 "s_invis" invisibility 101     6,11 "s_nostate" (no such icon_state)     8,11 "s_noicon1" (icon null)
//   9,14 "s_blank" (transparent state)  11,14 "s_blank2" (same, mouse_opacity 2)  13,14 "s_noicon" (icon null, mouse_opacity 2)
//   2,14 "s_ov" with an overlay 32 px east     6,14 "s_img" with a client.image 32 px east     2,1 "s_ov64", overlay 64 px east
// Map objs alone on their turf: 2,10 named ""   4,10 " "   6,10 "\improper"   8,10 "m_blank" (transparent state)
//   10,10 "m_blank2" (same, mouse_opacity 2)   12,10 "m_noicon" (icon null, mouse_opacity 2)   2,11 "m_noicon1" (icon null)
//   4,11 "m_nostate"   2,12 "m_px16" (pixel_x 16)   13,10 "m_py16" (pixel_y 16)   5,12 "m_invis" (invisibility 101)
//   7,12 "m_alpha0"   9,12 "m_mo2" (mouse_opacity 2) under "m_mo1"   11,12 "m_ov", overlay 32 px east
//   13,12 "m_img", client.image 32 px east   10,13 "m_ov64", overlay 64 px east
// Layer bands, plane 0 unless named otherwise (BACKGROUND_LAYER 20000, EFFECTS_LAYER 5000):
//   3,9 "s_ovonly" (plane 24, icon null, overlay 32 px east)   6,9 map "m_eff" (EFFECTS_LAYER+1) under screen "s_eff" (layer 1)
//   8,9 screen "s_bg" (BACKGROUND_LAYER+1)   10,9 map "m_bg" (BACKGROUND_LAYER+1)
//   12,9 screen "s_e1" (EFFECTS_LAYER+1) over map "m_e2" (EFFECTS_LAYER+2)
//   14,9 screen "s_bg1" (BACKGROUND_LAYER+1) over map "m_bg2" (BACKGROUND_LAYER+2)
//   2,15 screen "s_l1" (layer 1), then screen "s_bgl" (BACKGROUND_LAYER+1) joining later
//   4,15 screen "s_4999" over map "m_5000"   6,15 map "m_5000h" (5000.5) under screen "s_l1b" (layer 1)
//   8,15 map "m_20000" and 10,15 map "m_20000h" (20000.5), alone   12,15 screen "s_neg" (-30000) over map "m_neg" (-30000)
//   14,15 screen "s_huge" (1e7) under map "m_huge" (layer 3)
world
	name = "Fugue HUD Stack"
	view = 7
	turf = /turf/floor
	mob = /mob/player
	maxx = 15
	maxy = 15
	maxz = 1

turf/floor
	icon = 'thing.dmi'
	icon_state = "floor"

obj/slot
	icon = 'thing.dmi'
	icon_state = "slot"
	plane = 24
	layer = 1

obj/button
	name = "button"
	icon = 'thing.dmi'
	icon_state = "item"
	plane = 24
	layer = 2

obj/item
	icon = 'thing.dmi'
	icon_state = "item"
	verb/toggle()
		set name = "Toggle Suit Sensors"
		set category = "Object"
		set src in usr
		world.log << "TOGGLE [name] \ref[src]"
		usr << "toggle [name]"
	verb/inspect()
		set name = "Inspect Close"
		set src in view(1)
		world.log << "INSPECT [name] \ref[src]"
		usr << "inspect [name]"

mob/player
	icon = 'thing.dmi'
	icon_state = "player"

mob/player/proc/slot(nm, loc)
	var/obj/slot/s = new
	s.name = nm
	s.screen_loc = loc
	client.screen += s

mob/player/proc/wear(nm, loc)
	var/obj/item/i = new(src)
	i.name = nm
	i.plane = 24
	i.layer = 2
	i.screen_loc = loc
	client.screen += i

obj/clicker
	name = ""
	icon = 'thing.dmi'
	icon_state = "blank"
	plane = -99
	mouse_opacity = 2

obj/thing
	icon = 'thing.dmi'
	icon_state = "item"

mob/player/proc/scr(nm, st, pl, ly, mo, loc)
	var/obj/button/b = new
	b.name = nm
	b.icon_state = st
	b.plane = pl
	b.layer = ly
	b.mouse_opacity = mo
	b.screen_loc = loc
	client.screen += b
	return b

proc/put(nm, st, x, y, pl, ly, mo)
	var/obj/thing/t = new(locate(x, y, 1))
	t.name = nm
	t.icon_state = st
	t.plane = pl
	t.layer = ly
	t.mouse_opacity = mo
	return t

mob/player/Login()
	..()
	loc = locate(8, 8, 1)
	name = "player"
	slot("i_clothing", "3,3")
	wear("jumpsuit", "3,3")
	wear("gloves", "5,3")
	slot("i_gloves", "5,3")
	slot("i_shoes", "7,3")
	var/obj/button/b = new
	b.screen_loc = "9,3"
	client.screen += b
	slot("i_belt", "9,3")
	slot("e_first", "11,3")
	slot("e_second", "11,3")
	wear("shifted", "13:16,3")
	slot("g_slot", "3,5")
	var/obj/button/g = new
	g.name = "ghost"
	g.mouse_opacity = 0
	g.screen_loc = "3,5"
	client.screen += g
	var/obj/clicker/c = new
	c.screen_loc = "1,1 to 15,15"
	client.screen += c
	scr("under", "slot", -50, 1, 1, "7,5")
	scr("mo2", "item", 24, 2, 2, "9,5")
	scr("", "slot", 24, 1, 1, "11,5")
	scr(" ", "slot", 24, 1, 1, "1,7")
	scr("\improper", "slot", 24, 1, 1, "3,7")
	scr("s_p0", "item", 0, 1, 1, "7,7")
	put("m_p0_l5", "item", 7, 7, 0, 5, 1)
	put("m_p0_l1", "item", 7, 7, 0, 1, 1)
	scr("s_m50", "item", -50, 1, 1, "9,7")
	put("m_m60", "item", 9, 7, -60, 3, 1)
	put("m_m40", "item", 9, 7, -40, 3, 1)
	scr("s_p24", "item", 24, 1, 1, "11,7")
	put("m_p24_l30", "item", 11, 7, 24, 30, 1)
	put("m_p30", "item", 11, 7, 30, 3, 1)
	var/obj/button/iv = scr("s_invis", "slot", 24, 1, 1, "13,7")
	iv.invisibility = 101
	scr("s_nostate", "nonexistent", 24, 1, 1, "6,11")
	var/obj/button/sn1 = scr("s_noicon1", "item", 24, 1, 1, "8,11")
	sn1.icon = null
	scr("s_blank", "blank", 24, 1, 1, "9,14")
	scr("s_blank2", "blank", 24, 1, 2, "11,14")
	var/obj/button/sn = scr("s_noicon", "item", 24, 1, 2, "13,14")
	sn.icon = null
	var/obj/button/so = scr("s_ov", "item", 24, 1, 1, "2,14")
	so.overlays += image('thing.dmi', icon_state = "item", pixel_x = 32)
	var/obj/button/si = scr("s_img", "item", 24, 1, 1, "6,14")
	var/image/J = image('thing.dmi', si, "item")
	J.pixel_x = 32
	client.images += J
	var/obj/button/so64 = scr("s_ov64", "item", 24, 1, 1, "2,1")
	so64.overlays += image('thing.dmi', icon_state = "item", pixel_x = 64)
	put("", "item", 2, 10, 0, 3, 1)
	put(" ", "item", 4, 10, 0, 3, 1)
	put("\improper", "item", 6, 10, 0, 3, 1)
	put("m_blank", "blank", 8, 10, 0, 3, 1)
	put("m_blank2", "blank", 10, 10, 0, 3, 2)
	var/obj/thing/ni = put("m_noicon", "item", 12, 10, 0, 3, 2)
	ni.icon = null
	var/obj/thing/n1 = put("m_noicon1", "item", 2, 11, 0, 3, 1)
	n1.icon = null
	put("m_nostate", "nonexistent", 4, 11, 0, 3, 1)
	var/obj/thing/p16 = put("m_px16", "item", 2, 12, 0, 3, 1)
	p16.pixel_x = 16
	var/obj/thing/py = put("m_py16", "item", 13, 10, 0, 3, 1)
	py.pixel_y = 16
	var/obj/thing/inv = put("m_invis", "item", 5, 12, 0, 3, 1)
	inv.invisibility = 101
	var/obj/thing/a0 = put("m_alpha0", "item", 7, 12, 0, 3, 1)
	a0.alpha = 0
	put("m_mo2", "item", 9, 12, 0, 3, 2)
	put("m_mo1", "item", 9, 12, 0, 4, 1)
	var/obj/thing/ov = put("m_ov", "item", 11, 12, 0, 3, 1)
	ov.overlays += image('thing.dmi', icon_state = "item", pixel_x = 32)
	var/obj/thing/im = put("m_img", "item", 13, 12, 0, 3, 1)
	var/image/I = image('thing.dmi', im, "item")
	I.pixel_x = 32
	client.images += I
	var/obj/thing/o64 = put("m_ov64", "item", 10, 13, 0, 3, 1)
	o64.overlays += image('thing.dmi', icon_state = "item", pixel_x = 64)
	addBands()

var/list/bands = list(
	"m_eff", 6, 9, 5001, "s_eff", 6, 9, 1,
	"s_bg", 8, 9, 20001, "m_bg", 10, 9, 20001,
	"m_e2", 12, 9, 5002, "s_e1", 12, 9, 5001,
	"m_bg2", 14, 9, 20002, "s_bg1", 14, 9, 20001,
	"s_l1", 2, 15, 1, "s_bgl", 2, 15, 20001,
	"m_5000", 4, 15, 5000, "s_4999", 4, 15, 4999,
	"m_5000h", 6, 15, 5000.5, "s_l1b", 6, 15, 1,
	"m_20000", 8, 15, 20000, "m_20000h", 10, 15, 20000.5,
	"m_neg", 12, 15, -30000, "s_neg", 12, 15, -30000,
	"m_huge", 14, 15, 3, "s_huge", 14, 15, 1e7)

mob/player/proc/addBands()
	var/obj/button/s1 = scr("s_ovonly", "item", 24, 1, 1, "3,9")
	s1.icon = null
	s1.overlays += image('thing.dmi', icon_state = "item", pixel_x = 32)
	for(var/i = 1, i <= bands.len, i += 4)
		var/nm = bands[i]
		if(copytext(nm, 1, 2) == "s")
			scr(nm, "item", 0, bands[i+3], 1, "[bands[i+1]],[bands[i+2]]")
		else
			put(nm, "item", bands[i+1], bands[i+2], 0, bands[i+3], 1)

client/Click(object)
	world.log << "CLICK [object]"
	..()

mob/verb/examination(atom/A as mob|obj|turf in view(src))
	set name = "Examine"
	set category = "IC"
	world.log << "EXAMINE [A] \ref[A]"
	usr << "examine [A]"

mob/verb/look(atom/A as mob|obj|turf in view())
	set name = "Look"
	set category = "IC"
	world.log << "LOOK [A] \ref[A]"
	usr << "look [A]"

mob/verb/pulled(atom/movable/AM as mob|obj in oview(1))
	set name = "Pull"
	set category = "Object"
	world.log << "PULL [AM] \ref[AM]"
	usr << "pull [AM]"
