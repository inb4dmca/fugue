// A plane drawn through a relay: plane 1's master (screen_loc 1,1) has a render target, and a screen object `relay`
// on plane 5 draws it through render_source. Run with -params "set=<case>" (see set_case()). Every atom logs Click()
// and has a verb of its own, so that an entry of the right-click menu tells which atom it is.
world
    name = "Plane relay probe"
    maxx = 7
    maxy = 7
    maxz = 1
    view = 3
    fps = 10
    mob = /mob/observer
/atom/Click(location, control, params)
    var/list/p = params2list(params)
    world.log << "CLICK [name] x=[p["icon-x"]] y=[p["icon-y"]]"
/turf
    icon = 'floor.png'
    name = "floor"
    verb/poke_floor()
        set src in world
        world.log << "VERB floor"
/obj/part
    icon = 'box.png'
    plane = 1
    name = "part"
    verb/poke_part()
        set src in world
        world.log << "VERB part"
/obj/other
    icon = 'blue.png'
    plane = 1
    name = "other"
    verb/poke_other()
        set src in world
        world.log << "VERB other"
/obj/marker
    icon = 'blue.png'
    name = "marker"
// An ordinary screen object above the relay, put at the relay's screen_loc (case word `mark`).
/obj/mark
    icon = 'yellow.png'
    plane = 6
    name = "mark"
    verb/poke_mark()
        set src in world
        world.log << "VERB mark"
// A HUD object on the relayed plane.
/obj/button
    icon = 'green.png'
    plane = 1
    name = "button"
    screen_loc = "6,2"
    verb/poke_button()
        set src in world
        world.log << "VERB button"
/obj/master
    appearance_flags = PLANE_MASTER
    plane = 1
    name = "master"
    screen_loc = "1,1"
    render_target = "*p1"
    verb/poke_master()
        set src in world
        world.log << "VERB master"
// As SS13 codebases relay their planes.
/obj/relay
    appearance_flags = PASS_MOUSE | KEEP_TOGETHER | NO_CLIENT_COLOR
    plane = 5
    name = "relay"
    screen_loc = "1,1"
    render_source = "*p1"
    verb/poke_relay()
        set src in world
        world.log << "VERB relay"
/proc/turned(angle)
    var/matrix/M = matrix()
    M.Turn(angle)
    return M
/proc/set_case(client/C, kind)
    var/obj/master/M = new
    if(findtext(kind, "visible")) M.render_target = "p1"   // the plane draws itself as well
    if(findtext(kind, "mturn90")) M.transform = turned(90)
    if(findtext(kind, "mscalex2")) M.transform = matrix(2, 0, 0, 0, 1, 0)
    if(findtext(kind, "malpha")) M.alpha = 254
    C.screen += M
    C.screen += new /obj/button
    if(findtext(kind, "norelay"))
        new /obj/marker(locate(6, 6, 1))   // so that the map is not one colour (stockprobe waits for a drawn map)
        world.log << "CASE [kind]"
        return
    var/obj/relay/R = new
    if(findtext(kind, "visible")) R.render_source = "p1"
    if(findtext(kind, "nopass")) R.appearance_flags &= ~PASS_MOUSE
    if(findtext(kind, "nokt")) R.appearance_flags &= ~KEEP_TOGETHER
    if(findtext(kind, "loc23")) R.screen_loc = "2,3"
    if(findtext(kind, "locpx")) R.screen_loc = "2:5,3:-7"
    if(findtext(kind, "loccenter")) R.screen_loc = "CENTER-2,CENTER-1"   // 2,3 in a 7x7 view
    if(findtext(kind, "locne")) R.screen_loc = "EAST-5,NORTH-4"   // 2,3 as well
    if(findtext(kind, "locleft")) R.screen_loc = "LEFT+1,BOTTOM+2"   // edge anchors
    if(findtext(kind, "locright")) R.screen_loc = "RIGHT-5,TOP-4"
    if(findtext(kind, "locroff")) R.screen_loc = "RIGHT-5:4,TOP-4:-3"
    if(findtext(kind, "locws")) R.screen_loc = "WEST+1,SOUTH+2"
    if(findtext(kind, "locpb")) R.screen_loc = "1+25%,1+40%"
    if(findtext(kind, "locpd")) R.screen_loc = "1+10%,1+10%"
    if(findtext(kind, "locpe")) R.screen_loc = "1-20%,1-20%"
    if(findtext(kind, "locpf")) R.screen_loc = "RIGHT-90%,TOP-90%"
    if(findtext(kind, "locpg")) R.screen_loc = "1+40%,1+25%"
    if(findtext(kind, "locrange")) R.screen_loc = "1,1 to 3,3"   // ranges
    if(findtext(kind, "locrow")) R.screen_loc = "2,3 to 4,3"
    if(findtext(kind, "rpx"))   // the relay's own offsets
        R.pixel_x = 10
        R.pixel_y = 5
    if(findtext(kind, "rpw"))
        R.pixel_w = 10
        R.pixel_z = 5
    if(findtext(kind, "rturn90")) R.transform = turned(90)
    if(findtext(kind, "rturn45")) R.transform = turned(45)
    if(findtext(kind, "rscalex2")) R.transform = matrix(2, 0, 0, 0, 1, 0)
    if(findtext(kind, "rhalf")) R.transform = matrix(0.5, 0, 0, 0, 0.5, 0)
    if(findtext(kind, "rflip")) R.transform = matrix(-1, 0, 0, 0, 1, 0)
    if(findtext(kind, "rmove")) R.transform = matrix(1, 0, 10, 0, 1, 5)
    if(findtext(kind, "rgrowmove")) R.transform = matrix(1.5, 0, 100, 0, 1.5, 0)
    if(findtext(kind, "rturnmove"))   // turned by 30 degrees, then moved 20 right and 10 up
        var/matrix/T = turned(30)
        T.Translate(20, 10)
        R.transform = T
    var/at = findtext(kind, "rm_")   // rm_a_b_c_d_e_f: any matrix on the relay
    if(at)
        var/list/v = splittext(copytext(kind, at + 3), "_")
        R.transform = matrix(text2num(v[1]), text2num(v[2]), text2num(v[3]), text2num(v[4]), text2num(v[5]), text2num(v[6]))
    at = findtext(kind, "rt_")   // rt_<degrees>: the relay turned (last in the case name)
    if(at) R.transform = turned(text2num(copytext(kind, at + 3)))
    C.screen += R
    if(findtext(kind, "mark"))
        var/obj/mark/K = new
        K.screen_loc = R.screen_loc
        C.screen += K
    world.log << "CASE [kind]"
world/New()
    ..()
    world.log = file("events.log")
    new /obj/part(locate(5, 4, 1))
    new /obj/other(locate(2, 5, 1))
/mob/observer
    invisibility = 101
    Login()
        ..()
        loc = locate(4, 4, 1)
        set_case(client, world.params["set"])
        world.log << "READY [key]"
