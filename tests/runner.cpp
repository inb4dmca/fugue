#include <QtCore/QByteArray>
#include <QtCore/qtenvironmentvariables.h>
#include <cstring>
#include <iostream>

#define FUGUE_TEST_SUITE(name, entry) int entry(int argc, char** argv);
#include "fugue_test_suites.inc"
#undef FUGUE_TEST_SUITE

namespace {
struct Suite {
    const char* name;
    int (*run)(int, char**);
};
constexpr Suite suites[] = {
#define FUGUE_TEST_SUITE(name, entry) {name, entry},
#include "fugue_test_suites.inc"
#undef FUGUE_TEST_SUITE
};

void listSuites() {
    std::cout << "Usage: fugue_tests <suite> [QtTest arguments]\n\nSuites:\n";
    for (const auto& suite : suites) std::cout << "  " << suite.name << '\n';
}
} // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    // Without a console window, Qt otherwise sends plain test output to the
    // debugger instead of CTest's capture pipe (including failures and skips).
    qputenv("QT_FORCE_STDERR_LOGGING", "1");
#endif
    if (argc < 2 || std::strcmp(argv[1], "--list") == 0 || std::strcmp(argv[1], "--help") == 0) {
        listSuites();
        return 0;
    }
    for (const auto& suite : suites) {
        if (std::strcmp(argv[1], suite.name) != 0) continue;
        // Keep argv[0] as the executable path and remove only our selector.
        // The suite creates its own Qt application, if it needs one.
        for (int i = 1; i < argc; ++i) argv[i] = argv[i + 1];
        return suite.run(argc - 1, argv);
    }
    std::cerr << "Unknown test suite: " << argv[1] << '\n';
    listSuites();
    return 1;
}
