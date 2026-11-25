// The backend a native test entry runs on. Entries name it in FUGUE_GPU_BACKEND, which MapWidget reads itself.
#pragma once
#include "shell/mapwidget.h"
#include <QtTest>

namespace fugue::shell::test {
inline bool backendIsOpenGL() { return qgetenv("FUGUE_GPU_BACKEND").toLower() == "opengl"; }
// Empty when a shown MapWidget draws with the backend the entry names, or the entry names none (a run by hand);
// otherwise what went wrong. Asking for a backend is not having it: without this two entries could pass on one.
inline QString backendMismatch() {
    const QByteArray asked = qgetenv("FUGUE_GPU_BACKEND").toLower();
    if (asked.isEmpty()) return {};
    const QString expected = asked == "opengl" ? "OpenGL" : asked == "d3d11" ? "D3D11" : asked == "metal" ? "Metal" : QString();
    if (expected.isEmpty()) return "FUGUE_GPU_BACKEND names an unknown backend: " + QString::fromLatin1(asked);
    MapWidget map; map.resize(64, 64); map.setViewSize({1, 1}); map.show();
    if (!QTest::qWaitForWindowExposed(&map)) return "a MapWidget was not shown";
    for (int waited = 0; map.createdBackend().isEmpty() && waited < 5000; waited += 20) { map.grabFramebuffer(); QTest::qWait(20); }
    return map.createdBackend() == expected ? QString()
        : "FUGUE_GPU_BACKEND asks for " + expected + " but the widget created '" + map.createdBackend() + "'";
}
}
