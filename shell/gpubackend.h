// The one graphics backend of the process. FUGUE_GPU_BACKEND (docs/USAGE.md) is the only supported way to choose it.
// A window has one graphics device, which the map, Qt Quick and browser content in it all draw with, so they must
// agree. Qt Quick and WebEngine follow QSG_RHI_BACKEND, the map follows ours: settleGpuBackend() makes both name
// the same backend, always, before anything of Qt's reads them. Header-only, QtCore alone.
#pragma once
#include <QtCore/QByteArray>
#include <QtCore/QDebug>
#include <QtCore/QLibrary>
#include <cstdint>
#include <QtCore/qtenvironmentvariables.h>

namespace fugue::shell {
// Whether this machine can draw a window with Vulkan: its loader is installed, an instance that can present to a
// window can be made, and at least one graphics device can be opened with a swapchain. Listing a device is not
// enough: a broken or compute-only driver lists one and then fails, which would leave the map blank. Asked of the
// loader itself, since Qt's own Vulkan types need an application object and the Vulkan headers, and this runs
// before either. The structures are Vulkan 1.0's VkApplicationInfo, VkInstanceCreateInfo, VkDeviceQueueCreateInfo
// and VkDeviceCreateInfo, which never change; a pointer-sized handle and a 32-bit result are how every 64-bit
// loader passes its handles and VkResult. Every device has a queue family 0 with at least one queue.
inline bool vulkanAvailable() {
#if defined(Q_OS_WIN) && defined(Q_PROCESSOR_X86_64)
    struct Application { std::int32_t type = 0; const void* next = nullptr; const char* name = "fugue";
                         std::uint32_t version = 0; const char* engine = nullptr; std::uint32_t engineVersion = 0;
                         std::uint32_t api = 1u << 22; };   // Vulkan 1.0
    struct Create { std::int32_t type = 1; const void* next = nullptr; std::uint32_t flags = 0;
                    const Application* application = nullptr; std::uint32_t layers = 0; const char* const* layerNames = nullptr;
                    std::uint32_t extensions = 0; const char* const* extensionNames = nullptr; };
    struct Queue { std::int32_t type = 2; const void* next = nullptr; std::uint32_t flags = 0; std::uint32_t family = 0;
                   std::uint32_t count = 1; const float* priorities = nullptr; };
    struct Device { std::int32_t type = 3; const void* next = nullptr; std::uint32_t flags = 0;
                    std::uint32_t queues = 1; const Queue* queue = nullptr; std::uint32_t layers = 0;
                    const char* const* layerNames = nullptr; std::uint32_t extensions = 0;
                    const char* const* extensionNames = nullptr; const void* features = nullptr; };
    QLibrary loader(QStringLiteral("vulkan-1"));
    if (!loader.load()) return false;
    const auto create = reinterpret_cast<std::int32_t (*)(const Create*, const void*, void**)>(loader.resolve("vkCreateInstance"));
    const auto devices = reinterpret_cast<std::int32_t (*)(void*, std::uint32_t*, void**)>(loader.resolve("vkEnumeratePhysicalDevices"));
    const auto open = reinterpret_cast<std::int32_t (*)(void*, const Device*, const void*, void**)>(loader.resolve("vkCreateDevice"));
    const auto close = reinterpret_cast<void (*)(void*, const void*)>(loader.resolve("vkDestroyDevice"));
    const auto destroy = reinterpret_cast<void (*)(void*, const void*)>(loader.resolve("vkDestroyInstance"));
    if (!create || !devices || !open || !close || !destroy) return false;
    const char* const presenting[] = {"VK_KHR_surface", "VK_KHR_win32_surface"};
    const Application application; Create request; request.application = &application;
    request.extensions = 2; request.extensionNames = presenting;
    void* instance = nullptr;
    if (create(&request, nullptr, &instance) != 0 || !instance) return false;
    void* found[8] = {}; std::uint32_t count = 8;
    const std::int32_t listed = devices(instance, &count, found);   // 5: more than eight, the first eight are given
    bool usable = false;
    if (listed == 0 || listed == 5) {
        const float priority = 1; Queue queue; queue.priorities = &priority;
        const char* const swapchain[] = {"VK_KHR_swapchain"};
        Device wanted; wanted.queue = &queue; wanted.extensions = 1; wanted.extensionNames = swapchain;
        for (std::uint32_t i = 0; i < count && !usable; ++i) {
            void* device = nullptr;
            if (open(found[i], &wanted, nullptr, &device) == 0 && device) { usable = true; close(device, nullptr); }
        }
    }
    destroy(instance, nullptr);
    return usable;
#else
    return false;
#endif
}
// Call first thing in main(). Afterwards FUGUE_GPU_BACKEND and QSG_RHI_BACKEND hold the same name, one of d3d11,
// metal, opengl, vulkan: the one asked for or, when none or an unknown one was, the platform's usual one. That is
// Vulkan on Windows, or Direct3D 11 on a machine without Vulkan; OpenGL on Linux; and Metal on macOS unless
// metalWorks is given and says no, then OpenGL (Qt makes the same fallback on its own, but only while its variable
// is unset). A backend asked for by name is used as given and fails visibly where it does not exist.
inline QByteArray settleGpuBackend(bool (*metalWorks)() = nullptr) {
    QByteArray backend = qgetenv("FUGUE_GPU_BACKEND").toLower();
    const bool known = backend == "d3d11" || backend == "metal" || backend == "opengl" || backend == "vulkan";
    if (!known) {
        if (!backend.isEmpty())
            qWarning("FUGUE_GPU_BACKEND=%s is not d3d11, metal, opengl or vulkan; using the platform's backend",
                     backend.constData());
#if defined(Q_OS_WIN)
        backend = vulkanAvailable() ? "vulkan" : "d3d11";
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
