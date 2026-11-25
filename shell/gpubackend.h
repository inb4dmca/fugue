// The one graphics backend of the process. FUGUE_GPU_BACKEND (docs/USAGE.md) is the only supported way to choose it.
// A window has one graphics device, which the map, Qt Quick and browser content in it all draw with, so they must
// agree. Qt Quick and WebEngine follow QSG_RHI_BACKEND, the map follows ours: settleGpuBackend() makes both name
// the same backend, always, before anything of Qt's reads them. Header-only, QtCore alone.
#pragma once
#include <QtCore/QByteArray>
#include <QtCore/QDebug>
#include <QtCore/qtenvironmentvariables.h>

namespace fugue::shell {
// Call first thing in main(). Afterwards FUGUE_GPU_BACKEND and QSG_RHI_BACKEND hold the same name, one of d3d11,
// metal, opengl, vulkan: the one asked for or, when none or an unknown one was, the platform's usual one. The
// usual one on macOS is Metal unless metalWorks is given and says no, then OpenGL: Qt makes the same fallback on
// its own, but only while its variable is unset. A backend asked for by name is used as given and fails visibly
// where it does not exist.
inline QByteArray settleGpuBackend(bool (*metalWorks)() = nullptr) {
    QByteArray backend = qgetenv("FUGUE_GPU_BACKEND").toLower();
    const bool known = backend == "d3d11" || backend == "metal" || backend == "opengl" || backend == "vulkan";
    if (!known) {
        if (!backend.isEmpty())
            qWarning("FUGUE_GPU_BACKEND=%s is not d3d11, metal, opengl or vulkan; using the platform's backend",
                     backend.constData());
#if defined(Q_OS_WIN)
        backend = "d3d11";
        Q_UNUSED(metalWorks);
#elif defined(Q_OS_MACOS)
        backend = !metalWorks || metalWorks() ? "metal" : "opengl";
#else
        backend = "opengl";
        Q_UNUSED(metalWorks);
#endif
    }
    qputenv("FUGUE_GPU_BACKEND", backend);
    qputenv("QSG_RHI_BACKEND", backend);
    return backend;
}
} // namespace fugue::shell
