// A HUD plane master (plane 1, screen_loc 1,1) owning an overlay image and a world object on its plane, with a matrix on
// the master or on the overlay, or (cases starting "fx-") a master with a matrix, filters or both and no overlay; run
// with -params "set=<case>" (see set_master()). Cases starting "ax-" animate such a master (see animate_master()); only
// those with "tree" in the name keep the overlay.
world
    name = "Master transform probe"
    maxx = 7
    maxy = 7
    maxz = 1
    view = 3
    fps = 10
    mob = /mob/observer
/atom/Click(location, control, params)
    var/list/p = params2list(params)
    world.log << "CLICK [name] x=[p["icon-x"]] y=[p["icon-y"]] loc=[location]"
/turf
    icon = 'floor.png'
    name = "floor"
/obj/part
    icon = 'box.png'
    plane = 1
    name = "part"
/obj/edge
    icon = 'full.png'
    plane = 1
    name = "edge"
/obj/master
    appearance_flags = PLANE_MASTER
    plane = 1
    name = "master"
    screen_loc = "1,1"
/proc/turned(angle)
    var/matrix/M = matrix()
    M.Turn(angle)
    return M
/proc/set_master(obj/master/M, kind)
    var/image/I = image('blue.png')
    I.pixel_x = 64
    I.pixel_y = 32
    I.plane = 1
    if(findtext(kind, "child-turn")) I.transform = turned(45)
    if(findtext(kind, "child-scale")) I.transform = matrix(2, 0, 0, 0, 1, 0)
    if(findtext(kind, "child-lean")) I.transform = turned(30)
    if(!findtext(kind, "solo") && !findtext(kind, "fx-") && (!findtext(kind, "ax-") || findtext(kind, "tree"))) M.overlays += I
    if(findtext(kind, "group")) M.appearance_flags |= KEEP_TOGETHER
    if(findtext(kind, "master-turn")) M.transform = turned(90)
    if(findtext(kind, "master-scale")) M.transform = matrix(2, 0, 0, 0, 1, 0)
    if(findtext(kind, "master-shift")) M.transform = matrix(1, 0, 16, 0, 1, 8)
    if(findtext(kind, "master-triple")) M.transform = matrix(3, 0, 0, 0, 1, 0)
    if(findtext(kind, "master-grow")) M.transform = matrix(1.5, 0, 0, 0, 1.5, 0)
    if(findtext(kind, "scalex2")) M.transform = matrix(2, 0, 0, 0, 1, 0)
    if(findtext(kind, "turn90")) M.transform = turned(90)
    if(findtext(kind, "half")) M.transform = matrix(0.5, 0, 0, 0, 0.5, 0)
    if(findtext(kind, "pxs")) M.appearance_flags |= PIXEL_SCALE
    if(findtext(kind, "outline1")) M.filters += filter(type = "outline", size = 1, color = "#00ff00")
    if(findtext(kind, "outline4")) M.filters += filter(type = "outline", size = 4, color = "#00ff00")
    if(findtext(kind, "blur2")) M.filters += filter(type = "blur", size = 2)
    if(findtext(kind, "shadow6")) M.filters += filter(type = "drop_shadow", x = 6, y = 0, size = 0, color = "#ffffff")
    if(findtext(kind, "wave")) M.filters += filter(type = "wave", x = 16, y = 0, size = 3, flags = WAVE_SIDEWAYS)
    if(findtext(kind, "ripple")) M.filters += filter(type = "ripple", x = 0, y = 0, size = 6, repeat = 16, radius = 24, falloff = 1)
    if(findtext(kind, "bloom")) M.filters += filter(type = "bloom", threshold = rgb(100, 0, 0), size = 3, offset = 2, alpha = 255)
    if(findtext(kind, "swap")) M.filters += filter(type = "color", color = list(0,1,0, 1,0,0, 0,0,1))
    if(findtext(kind, "mask")) M.filters += filter(type = "alpha", icon = icon('mask.png'), x = 32, y = 0)
    if(findtext(kind, "ax-")) animate_master(M, kind)
    world.log << "CASE [kind]"
// ax-*: the master above, then animated. Each animation takes half a second and then holds its end for ten minutes
// (a second step to the same value), so a grab taken seconds after login sees the end of the first step while the
// animation is still running. With "mid" the first step takes a minute instead and there is no hold.
/proc/animate_master(obj/master/M, kind)
    var/time = findtext(kind, "mid") ? 600 : 5
    var/hold = !findtext(kind, "mid")
    var/parallel = 0
    if(findtext(kind, "grow2x") || findtext(kind, "spin90"))
        var/matrix/T = findtext(kind, "grow2x") ? matrix(2, 0, 0, 0, 1, 0) : turned(90)
        animate(M, transform = T, time = time)
        if(hold) animate(transform = T, time = 6000)
        parallel = ANIMATION_PARALLEL
    if(findtext(kind, "toshadow"))
        var/shadow = filter(type = "drop_shadow", x = 6, y = 0, size = 0, color = "#ffffff")
        animate(M, filters = shadow, time = time, flags = parallel)
        if(hold) animate(filters = shadow, time = 6000)
        return
    var/f = length(M.filters) ? M.filters[1] : null
    if(!f) return
    if(findtext(kind, "size4"))
        animate(f, size = 4, time = time, flags = parallel)
        if(hold) animate(size = 4, time = 6000)
    if(findtext(kind, "left"))
        animate(f, x = -6, time = time, flags = parallel)
        if(hold) animate(x = -6, time = 6000)
    if(findtext(kind, "phase"))
        animate(f, offset = 0.5, time = time, flags = parallel)
        if(hold) animate(offset = 0.5, time = 6000)
world/New()
    ..()
    world.log = file("events.log")
    new /obj/part(locate(5, 4, 1))
    if(findtext(world.params["set"], "edge")) new /obj/edge(locate(7, 4, 1))
/mob/observer
    invisibility = 101
    Login()
        ..()
        loc = locate(4, 4, 1)
        var/obj/master/M = new
        set_master(M, world.params["set"])
        client.screen += M
        world.log << "READY [key]"
