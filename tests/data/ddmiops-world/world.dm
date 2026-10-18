// DDMI ops: one object per case, each with an /icon built at runtime by /icon procs, which DreamDaemon ships as a DDMI
// recipe (or, past some size, as a raster). Login fcopy()s every finished icon to dmi/<case>.dmi: DreamDaemon's own
// result, the reference tests/ddmiops_test.cpp compares against. Each case sits at the bottom-left of its own 2x2 block. Arguments go through n() and globals so DreamMaker
// cannot fold them. Icons: mkdmi.py.
world
	name = "ddmiops"
	view = 12
	mob = /mob/player
	turf = /turf/floor
	maxx = 25
	maxy = 25
	maxz = 1

turf/floor

var/list/K = list(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32)
var/RED = "#ff0000"
var/GRN = "#00ff00"
var/BLU = "#0000ff"
var/GRY = "#808080"
var/HALF = 0.5
var/A200 = 200
var/list/cases = list()

proc/n(i)
	return K[i + 1]

proc/G(state)
	return icon('src.dmi', state)

proc/build()
	var/icon/I
	I = G("grad"); I.DrawBox(RED, n(5), n(5), n(20), n(20)); I.DrawBox(null, n(10), n(10), n(15), n(15)); cases["c_db2"] = I
	I = G("blank"); I.DrawBox(RED, n(7), n(9)); cases["c_dbpt"] = I
	I = G("grad"); I.DrawBox(null, n(3), n(3), n(12), n(12)); cases["c_dbnull"] = I
	I = G("grad"); I.DrawBox(rgb(n(255 > 32 ? 32 : 0), 0, 255, 128), n(20), n(20), n(5), n(5)); cases["c_dbrev"] = I
	I = G("blank")
	for(var/i = 1 to 10)
		I.DrawBox(rgb(i * 20, 255 - i * 20, i * 7), n(i), n(i * 2), n(i + 5), n(i * 3))
	cases["c_db10"] = I
	I = icon('canvas.dmi', "blank")
	for(var/i = 1 to 600)
		I.DrawBox(rgb(i % 256, (i * 7) % 256, 50), (i * 13) % 64 + 1, (i * 29) % 64 + 1)
	cases["c_dbmany"] = I
	I = icon('canvas.dmi', "blank")
	for(var/x = 1 to 64)
		for(var/y = 1 to 64)
			I.DrawBox(rgb(x * 4 - 1, y * 4 - 1, 9), x, y)
	cases["c_dbhuge"] = I
	I = G("grad"); I.DrawBox(RED, n(5), n(5), n(20), n(20)); I.Blend(GRY, ICON_MULTIPLY); cases["c_dbblend"] = I
	I = G("grad"); I.Blend(BLU, ICON_ADD); I.DrawBox(GRN, n(2), n(2), n(9), n(30)); cases["c_blenddb"] = I
	I = G("grad"); I.Blend(G("green"), ICON_OVERLAY); I.Blend(RED, ICON_ADD); cases["c_blend2"] = I
	I = icon('canvas.dmi', "blank"); I.Blend(G("grad"), ICON_OVERLAY, n(10), n(20)); cases["c_blendxy"] = I
	I = G("grad"); I.Shift(EAST, n(3)); cases["c_shift"] = I
	I = G("grad"); I.Shift(NORTH, n(5), 1); cases["c_shiftw"] = I
	I = G("grad"); I.Shift(SOUTHWEST, n(4)); cases["c_shiftsw"] = I
	I = G("green"); I.SwapColor(GRN, BLU); cases["c_swap"] = I
	I = G("green"); I.SwapColor(GRN, null); cases["c_swapnull"] = I
	I = G("grad"); I.SwapColor(null, "#000000"); cases["c_swapfromnull"] = I
	I = G("grad"); I.SwapColor(rgb(0, 247, 32, 255), rgb(1, 2, 3, 4)); cases["c_swaprgba"] = I
	I = G("grad"); I.Crop(n(5), n(5), n(20), n(24)); cases["c_crop"] = I
	I = G("grad"); I.Crop(-n(3), -n(3), n(32) + 8, n(32) + 8); cases["c_cropbig"] = I
	I = G("grad"); I.Scale(n(32) * 2, n(32) * 2); cases["c_scale64"] = I
	I = G("grad"); I.Scale(n(16), n(16)); cases["c_scale16"] = I
	I = G("grad"); I.Scale(n(32) + 16, n(20)); cases["c_scale48x20"] = I
	I = G("grad"); I.Flip(EAST); cases["c_flipe"] = I
	I = G("grad"); I.Flip(NORTH); cases["c_flipn"] = I
	I = G("grad"); I.Turn(n(30) * 3); cases["c_turn90"] = I
	I = G("grad"); I.Turn(n(15) * 3); cases["c_turn45"] = I
	I = turn(G("grad"), n(30) * 3); cases["c_turnproc"] = I
	I = G("grad") + G("green"); cases["c_addicon"] = I
	I = G("grad") * GRY; cases["c_mulc"] = I
	I = G("grad") - rgb(n(32), n(16), n(8)); cases["c_subc"] = I
	I = G("grad"); I.MapColors(0.3,0.3,0.3, 0.59,0.59,0.59, 0.11,0.11,HALF); cases["c_mc9"] = I
	I = G("grad"); I.MapColors(HALF,0,0, 0,1,0, 0,0,1, 0.1,0.2,0.3); cases["c_mc12"] = I
	I = G("grad"); I.MapColors(HALF,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,HALF); cases["c_mc16"] = I
	I = G("grad"); I.MapColors(RED, GRN, BLU); cases["c_mcstr3"] = I
	I = G("grad"); I.MapColors("#ff000080", GRN, BLU, "#00000080", "#10203000"); cases["c_mcstr5"] = I
	I = G("grad"); I.MapColors(1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,HALF, 0,0,0,0); cases["c_opac"] = I
	I = G("grad"); I.SetIntensity(HALF); cases["c_intens"] = I
	I = G("grad"); I.SetIntensity(HALF, 1, 2); cases["c_intens3"] = I
	I = G("grad"); I.GrayScale(); cases["c_gray"] = I
	I = G("grad"); I.ColorTone("#40a0ff"); cases["c_tone"] = I
	I = icon('src.dmi', "dirs", EAST); I.DrawBox(GRY, n(1), n(1), n(4), n(4)); cases["c_direast"] = I
	I = icon('src.dmi', "dirs"); I.DrawBox(GRY, n(1), n(1), n(4), n(4)); I.DrawBox(RED, n(30), n(30), n(32), n(32)); cases["c_dirall"] = I
	I = new(G("grad")); I.DrawBox(BLU, n(1), n(1), n(8), n(8)); var/icon/J = new(I); J.DrawBox(GRN, n(25), n(25), n(32), n(32)); cases["c_copy"] = J
	I = G("grad"); I.MapColors(0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, -1,-1,-1,-1); I.DrawBox(RED, n(1), n(16), n(16), n(32)); I.DrawBox(BLU, n(17), n(1), n(32), n(16)); cases["c_mcdb2"] = I
	I = G("grad"); I.MapColors(0.3,0.3,0.3, 0.59,0.59,0.59, 0.11,0.11,HALF); I.DrawBox(RED, n(3), n(3), n(9), n(9)); I.Shift(EAST, n(2)); I.Turn(n(30) * 3); I.Blend(G("green"), ICON_MULTIPLY); cases["c_seq"] = I
	I = G("blank"); I.Insert(G("grad"), "ins"); cases["c_insert"] = I
	I = G("grad"); I.Blend(G("green"), ICON_OVERLAY); I.SwapColor(rgb(0,255,0,255), rgb(0,0,0,0)); cases["c_wincompat"] = I
	I = G("grad"); I.Blend(G("green"), ICON_UNDERLAY); I.Blend(G("green"), ICON_SUBTRACT); cases["c_blendii"] = I
	I = G("grad"); I.DrawBox(RED, -n(3), n(2), n(5), n(6)); cases["d_dbneg"] = I
	I = G("grad"); I.DrawBox(BLU, n(28), n(30), n(32) + 9, n(32) + 9); cases["d_dbover"] = I
	I = G("grad"); I.DrawBox(null, n(1), n(5), n(32), n(32)); I.Turn(n(30) + 7); cases["d_beamcut"] = I
	I = G("grad"); I.SwapColor(rgb(n(8), 239, 128), BLU); cases["d_swapsemi"] = I
	I = G("grad"); I.SwapColor(rgb(n(8), 239, 128), "#0000ff40"); cases["d_swapsemi2"] = I
	I = G("grad"); I.SwapColor(rgb(n(0), 255, 32, 128), "#ff000080"); cases["d_swapexact"] = I
	I = G("grad"); I.SwapColor(rgb(n(0), 255, 32), null); cases["d_swaptonull"] = I
	I = G("grad"); I.Flip(NORTHEAST); cases["d_flipne"] = I
	I = G("grad"); I.Flip(SOUTHWEST); cases["d_flipsw"] = I
	I = G("grad"); I.Turn(n(30)); cases["d_turn30"] = I
	I = G("grad"); I.Turn(n(30) + 7); cases["d_turn37"] = I
	I = G("grad"); I.Turn(n(20) * 10); cases["d_turn200"] = I
	I = G("grad"); I.Turn(-n(20)); cases["d_turnm20"] = I
	I = G("grad"); I.Turn(n(30) * 9); cases["d_turn270"] = I
	I = G("grad"); I.Turn(-n(30) * 3); cases["d_turnm90"] = I
	I = G("grad"); I.Turn(n(30) * 6 + 0.5); cases["d_turn1805"] = I
	I = icon('canvas.dmi', "fill"); I.Turn(n(30) + 7); cases["d_turnbig"] = I
	I = G("grad"); I.Crop(n(1), n(1), n(32), n(20)); I.Turn(n(30) * 3); cases["d_turnrect"] = I
	I = G("grad"); I.Scale(n(20) + 1, n(32) + 5); cases["d_scale21x37"] = I
	I = G("grad"); I.Scale(n(10), n(7)); cases["d_scale10x7"] = I
	I = G("grad"); I.Crop(n(5), n(5), n(20), n(24)); I.DrawBox(RED, n(1), n(1), n(3), n(3)); I.Shift(EAST, n(1)); cases["d_cropops"] = I
	I = G("grad"); I.Blend(G("green"), ICON_OVERLAY, -n(5), n(10)); cases["d_blendneg"] = I
	I = G("grad"); I.Blend(RED, ICON_ADD, n(5), n(10)); cases["d_blendcolxy"] = I
	I = G("grad"); I.Blend(G("dirs"), ICON_MULTIPLY, n(3), n(1)); cases["d_blendxymul"] = I
	I = G("grad"); I.SetIntensity(n(2) + 0.5); cases["d_intens25"] = I
	I = G("grad"); I.MapColors(RED, GRN, BLU, "#ff0000", "#10203040"); cases["d_mcstr5b"] = I
	I = G("grad"); I.MapColors(rgb(n(20)*10,0,0), rgb(0,n(10)*10,0), rgb(0,0,n(25)*2)); cases["d_mchulk"] = I
	I = icon('src.dmi'); I.DrawBox(RED, n(1), n(1), n(8), n(8)); I.MapColors(0.3,0.3,0.3, 0.59,0.59,0.59, 0.11,0.11,HALF); cases["d_whole"] = I
	I = icon('src.dmi', "dirs", EAST); cases["d_direast"] = I
	I = icon('src.dmi', "dirs", NORTH); I.Turn(n(30) * 3); cases["d_dirnorth"] = I
	I = new /icon; I.Insert(G("grad"), null, frame = n(1), delay = n(5)); I.Insert(G("green"), null, frame = n(2), delay = n(5)); cases["d_insanim"] = I
	I = G("blank"); I.Insert(icon('src.dmi', "dirs", NORTH), "", dir = NORTH); I.Insert(icon('src.dmi', "dirs", EAST), "", dir = EAST); cases["d_insdir"] = I
	I = G("blank"); I.Insert(G("grad"), "x", SOUTH, 1, 0); cases["d_insfull"] = I
	I = G("grad"); I.DrawBox(RED, n(1), n(1)); I.Blend(G("green"), ICON_OVERLAY); I.Blend(RED, ICON_MULTIPLY); I.SwapColor(RED, GRN); I.Shift(WEST, n(2), 1); I.Flip(EAST); I.Scale(n(16), n(16)); cases["d_long"] = I
	I = G("grad"); I.Blend(G("green"), ICON_OVERLAY); var/icon/J2 = G("blank"); J2.Blend(I, ICON_OVERLAY); cases["d_nested"] = J2
	I = G("blank"); I.Insert(icon('src.dmi'), "x"); cases["e_inswhole"] = I
	I = new /icon; I.Insert(G("grad"), null, frame = n(1), delay = n(10) / 3); I.Insert(G("green"), null, frame = n(2), delay = n(10) / 3); cases["e_insdelay"] = I
	I = new /icon; I.Insert(G("grad"), "m", frame = n(1), moving = n(1), delay = n(2)); cases["e_insmoving"] = I
	I = icon('src.dmi', "dirs"); I.Insert(G("grad"), "", dir = EAST); cases["e_insreplace"] = I
	I = G("grad"); I.Insert(G("green"), "", frame = n(3)); cases["e_insframe3"] = I
	I = G("grad"); I.Insert(G("green")); cases["e_insnull"] = I
	I = G("blank"); I.Insert(icon('src.dmi', "dirs"), "y"); cases["e_insmulti"] = I
	I = new /icon; I.Insert(G("grad"), "a", dir = EAST, frame = n(2)); cases["e_insdirframe"] = I
	I = new /icon; I.Insert(G("grad"), "a"); I.Insert(G("green"), "b"); I.Insert(G("dirs"), "a", dir = NORTH); cases["e_insmix"] = I
	I = G("grad"); I.Insert(G("green"), "q"); I.DrawBox(RED, n(1), n(1), n(4), n(4)); cases["e_insthenop"] = I
	I = G("grad"); var/icon/J3 = G("green"); J3.Shift(EAST, n(4)); I.Insert(J3, "s"); cases["e_insmod"] = I
	I = icon('src.dmi', "dirs"); I.Turn(n(15)); cases["e_turndirs"] = I
	I = icon('src.dmi', "grad", SOUTH, n(1), n(0)); cases["f_frame1"] = I
	I = icon('src.dmi', "walk", EAST, n(2), n(0)); cases["f_walkf2"] = I
	I = icon('src.dmi', "walk", NORTH, n(3), n(1)); cases["f_walkmov"] = I
	I = icon('src.dmi', "walk"); cases["f_walkboth"] = I
	I = icon('src.dmi', "walk", frame = n(2)); cases["f_walkframe"] = I
	I = icon('src.dmi', "walk", moving = n(1)); cases["f_walkmv"] = I
	I = icon('src.dmi', "walk", WEST, moving = n(0)); cases["f_walkstill"] = I
	I = icon('src.dmi', dir = SOUTH, frame = n(1), moving = n(0)); cases["f_whole1"] = I
	I = icon('src.dmi', dir = EAST, frame = n(2)); cases["f_whole2"] = I
	I = icon('src.dmi', moving = n(1)); cases["f_wholemov"] = I
	I = icon('src.dmi', "eight", SOUTHEAST); cases["f_diag8"] = I
	I = icon('src.dmi', "eight", NORTHWEST, n(1), n(0)); cases["f_diag8nw"] = I
	I = icon('src.dmi', "dirs", SOUTHEAST); cases["f_diag4"] = I
	I = icon('src.dmi', "grad", EAST); cases["f_onedir"] = I
	I = icon('src.dmi', "walk", SOUTH, n(5)); cases["f_framebig"] = I
	I = icon('src.dmi', "nope"); cases["f_nostate"] = I
	cases["f_flat"] = Flat(SOUTH)
	cases["f_flatn"] = Flat(NORTH)
	var/icon/B = icon('src.dmi', "blank"); var/icon/R = icon(B, ""); R.Insert(Flat(SOUTH), "", dir = SOUTH); R.Insert(Flat(NORTH), "", dir = NORTH); R.Insert(Flat(EAST), "", dir = EAST); R.Insert(Flat(WEST), "", dir = WEST); cases["f_photo"] = R
	R = icon(icon('src.dmi', "blank"), ""); R.Insert(icon('src.dmi', "walk", SOUTH, n(1), n(0)), "", dir = SOUTH); R.Insert(icon('src.dmi', "walk", NORTH, n(2), n(0)), "", dir = NORTH); R.Insert(icon('src.dmi', "eight", EAST, n(1), n(0)), "", dir = EAST); cases["f_photo2"] = R
	B = icon('src.dmi', "walk"); B.Blend(RED, ICON_ADD); R = icon(B, "", NORTH, n(2), n(1)); cases["f_reicon"] = R
	B = icon('src.dmi', "dirs"); B.Turn(n(30) * 3); R = icon(B, dir = WEST); cases["f_reiconnone"] = R

// getFlatIconDeluxe's shape: every layer icon(file, state, dir, 1, 0), coloured, turned or faded, then blended in.
proc/Flat(d)
	var/icon/flat = icon('src.dmi', "blank")
	var/icon/add = icon('src.dmi', "grad", d == SOUTH ? d : SOUTH, n(1), n(0))
	add.Blend("#ff8080[copytext(rgb(0, 0, 0, A200), 8)]", ICON_MULTIPLY)
	flat.Blend(add, ICON_OVERLAY, 1 + n(2), 1)
	add = icon('src.dmi', "walk", d, n(1), n(0))
	add.Turn(n(30) * 3)
	add.MapColors(1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,HALF, 0,0,0,0)
	flat.Blend(add, ICON_OVERLAY, 1, 1 + n(3))
	add = icon('src.dmi', dir = d, frame = n(1), moving = n(0))
	add.Blend(RED, ICON_ADD)
	flat.Blend(add, ICON_OVERLAY, 1 - n(4), 1)
	flat.Crop(1, 1, 32, 32)
	return flat

mob/player
	Login()
		..()
		loc = locate(13, 13, 1)
		if(!cases.len)
			build()
			fdel("dmi/")
			for(var/c in cases)
				var/icon/I = cases[c]
				fcopy(I, "dmi/[c].dmi")
		var/i = 0
		for(var/c in cases)
			var/obj/o = new(locate(1 + 2 * (i % 12), 1 + 2 * round(i / 12), 1))
			o.name = c
			o.icon = cases[c]
			if(c == "c_insert") o.icon_state = "ins"
			if(c == "d_insfull") o.icon_state = "x"
			i++
		world.log << "PROBE built [i]"

/icon/proc/GrayScale()
	MapColors(0.3,0.3,0.3, 0.59,0.59,0.59, 0.11,0.11,0.11, 0,0,0)

/icon/proc/ColorTone(tone)
	GrayScale()
	var/list/TONE = list(0x40, 0xa0, 0xff)
	var/gray = round(TONE[1]*0.3 + TONE[2]*0.59 + TONE[3]*0.11, 1)
	var/icon/upper = (255-gray) ? new(src) : null
	if(gray)
		MapColors(255/gray,0,0, 0,255/gray,0, 0,0,255/gray, 0,0,0)
		Blend(tone, ICON_MULTIPLY)
	else
		SetIntensity(0)
	if(255-gray)
		upper.Blend(rgb(gray,gray,gray), ICON_SUBTRACT)
		upper.MapColors((255-TONE[1])/(255-gray),0,0,0, 0,(255-TONE[2])/(255-gray),0,0, 0,0,(255-TONE[3])/(255-gray),0, 0,0,0,0, 0,0,0,1)
		Blend(upper, ICON_ADD)
