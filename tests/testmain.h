#pragma once

#include <QtTest>

// Match QtTest's standalone entry points, but expose the suite to our runner.
// Each suite retains its own compile definitions and application lifetime.
#define FUGUE_TEST_APPLESS_MAIN(TestClass) \
    int FUGUE_TEST_ENTRY(int argc, char** argv) { \
        TestClass test; \
        QTest::setMainSourcePath(__FILE__); \
        return QTest::qExec(&test, argc, argv); \
    }

#define FUGUE_TEST_APPLICATION_MAIN(TestClass, Application) \
    int FUGUE_TEST_ENTRY(int argc, char** argv) { \
        Application app(argc, argv); \
        app.setAttribute(Qt::AA_Use96Dpi, true); \
        TestClass test; \
        QTest::setMainSourcePath(__FILE__); \
        return QTest::qExec(&test, argc, argv); \
    }

#define FUGUE_TEST_GUILESS_MAIN(TestClass) FUGUE_TEST_APPLICATION_MAIN(TestClass, QCoreApplication)

#if defined(QT_WIDGETS_LIB)
#define FUGUE_TEST_MAIN(TestClass) FUGUE_TEST_APPLICATION_MAIN(TestClass, QApplication)
#elif defined(QT_GUI_LIB)
#define FUGUE_TEST_MAIN(TestClass) FUGUE_TEST_APPLICATION_MAIN(TestClass, QGuiApplication)
#else
#define FUGUE_TEST_MAIN(TestClass) FUGUE_TEST_GUILESS_MAIN(TestClass)
#endif
