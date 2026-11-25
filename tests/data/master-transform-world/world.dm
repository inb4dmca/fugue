// A HUD plane master (plane 1, screen_loc 1,1) owning an overlay image and a world object on its plane, with a matrix on
// the master or on the overlay, or (cases starting "fx-") a master with a matrix, filters or both and no overlay; run
// with -params "set=<case>" (see set_master()).
world
    name = "Master transform probe"
    maxx = 7
    maxy = 7
    maxz = 1
    view = 3
    fps = 10
    mob = /mob/observer
/turf
    icon = 'floor.png'
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
    if(!findtext(kind, "solo") && !findtext(kind, "fx-")) M.overlays += I
    if(findtext(kind, "group")) M.appearance_flags |= KEEP_TOGETHER
    if(findtext(kind, "master-turn")) M.transform = turned(90)
    if(findtext(kind, "master-scale")) M.transform = matrix(2, 0, 0, 0, 1, 0)
    if(findtext(kind, "master-shift")) M.transform = matrix(1, 0, 16, 0, 1, 8)
    if(findtext(kind, "scalex2")) M.transform = matrix(2, 0, 0, 0, 1, 0)
    if(findtext(kind, "turn90")) M.transform = turned(90)
    if(findtext(kind, "half")) M.transform = matrix(0.5, 0, 0, 0, 0.5, 0)
    if(findtext(kind, "outline1")) M.filters += filter(type = "outline", size = 1, color = "#00ff00")
    if(findtext(kind, "outline4")) M.filters += filter(type = "outline", size = 4, color = "#00ff00")
    if(findtext(kind, "blur2")) M.filters += filter(type = "blur", size = 2)
    if(findtext(kind, "shadow6")) M.filters += filter(type = "drop_shadow", x = 6, y = 0, size = 0, color = "#ffffff")
    world.log << "CASE [kind]"
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
