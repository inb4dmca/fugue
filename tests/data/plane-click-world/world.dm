// Clicks under a HUD plane master (plane 1, screen_loc 1,1) with a matrix, filters or both, over a world object, a
// client image and a vis_contents member on its plane; run with -params "set=<case>" (see set_master()). Every atom
// logs Click() and has a verb of its own, so that an entry of the right-click menu tells which atom it is.
world
    name = "Plane click probe"
    maxx = 7
    maxy = 7
    maxz = 1
    view = 3
    fps = 10
    mob = /mob/observer
var/list/pictures = list()
var/list/hud = list()
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
// Shows the client image (green) and nothing of its own.
/obj/holder
    name = "holder"
    verb/poke_holder()
        set src in world
        world.log << "VERB holder"
// Shows the vis_contents member and nothing of its own.
/obj/visholder
    name = "visholder"
    verb/poke_visholder()
        set src in world
        world.log << "VERB visholder"
/obj/child
    icon = 'blue.png'
    plane = 1
    name = "child"
    verb/poke_child()
        set src in world
        world.log << "VERB child"
// A world object turned by 45 degrees on plane 0, for the right-click menu's rule on a turned icon.
/obj/leaner
    icon = 'box.png'
    name = "leaner"
    verb/poke_leaner()
        set src in world
        world.log << "VERB leaner"
// The source of a render-target mask: the left half of a tile.
/obj/maskatom
    icon = 'mask.png'
    name = "maskatom"
    render_target = "*maskrt"
/obj/maskplanepart
    icon = 'mask.png'
    plane = 2
    name = "maskplanepart"
/obj/master
    appearance_flags = PLANE_MASTER
    plane = 1
    name = "master"
    screen_loc = "1,1"
    verb/poke_master()
        set src in world
        world.log << "VERB master"
/obj/maskplane
    appearance_flags = PLANE_MASTER
    plane = 2
    name = "maskplane"
    screen_loc = "1,1"
    render_target = "*maskplane"
/proc/turned(angle)
    var/matrix/M = matrix()
    M.Turn(angle)
    return M
/proc/set_master(obj/master/M, kind)
    if(findtext(kind, "tree"))   // an overlay on the master, turned by 45 degrees, as master-transform-world's child-turn
        var/image/I = image('blue.png')
        I.pixel_x = 64
        I.pixel_y = 32
        I.plane = 1
        I.transform = turned(45)
        M.overlays += I
    if(findtext(kind, "turn90")) M.transform = turned(90)
    if(findtext(kind, "scalex2")) M.transform = matrix(2, 0, 0, 0, 1, 0)
    if(findtext(kind, "outline1")) M.filters += filter(type = "outline", size = 1, color = "#00ff00")
    var/flags = findtext(kind, "inverse") ? MASK_INVERSE : 0
    if(findtext(kind, "iconalpha")) M.filters += filter(type = "alpha", icon = icon('mask.png'), x = 32, y = 0, flags = flags)
    if(findtext(kind, "rtalpha")) M.filters += filter(type = "alpha", render_source = "*maskrt", x = 32, y = 0, flags = flags)
    if(findtext(kind, "ptalpha")) M.filters += filter(type = "alpha", render_source = "*maskplane", flags = flags)
    world.log << "CASE [kind]"
world/New()
    ..()
    world.log = file("events.log")
    var/kind = world.params["set"]
    new /obj/part(locate(5, 4, 1))
    var/obj/holder/H = new(locate(3, 4, 1))
    var/image/I = image('green.png', H)
    I.plane = 1
    pictures += I
    var/obj/visholder/V = new(locate(4, 6, 1))
    V.vis_contents += new /obj/child
    var/obj/leaner/L = new(locate(2, 6, 1))
    L.transform = turned(45)
    if(findtext(kind, "rtalpha")) new /obj/maskatom(locate(2, 2, 1))
    if(findtext(kind, "ptalpha"))
        new /obj/maskplanepart(locate(5, 4, 1))
        hud += new /obj/maskplane
/mob/observer
    invisibility = 101
    Login()
        ..()
        loc = locate(4, 4, 1)
        var/obj/master/M = new
        set_master(M, world.params["set"])
        client.screen += M
        client.screen += hud
        client.images += pictures
        world.log << "READY [key]"
