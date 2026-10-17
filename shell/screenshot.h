// The `.screenshot` and `.screenshot auto` client commands: the file naming and the save. Pure Qt Core and Gui, with
// no widgets, so it is unit-tested without a window. The app (app/main.cpp) owns the dialog and the chat line.
//
// Stock DreamSeeker 516 behaviour:
//   * What is saved is the map alone, at its native size (tiles x icon size), with no letterbox, window chrome or
//     on-screen zoom. `.screenshot` and `.screenshot auto` write identical files.
//   * `.screenshot auto` writes without a dialog to <BYOND user dir>/screenshots/"<world.name> YYYY-MM-DD HHMMSS.png"
//     and prints a chat line with the path. Characters a file name cannot hold are dropped, not replaced.
//   * `.screenshot` opens a "Save Screenshot As" dialog, filter "PNG Files (*.png)", with a default name that
//     contains a literal "(null)" from an unfilled format argument. A typed name gets ".png" appended. No chat line.
#pragma once
#include <QDateTime>
#include <QImage>
#include <QString>

namespace fugue::shell {

// "<world name, unsavable characters dropped> yyyy-MM-dd HHmmss" (no extension). An empty result name -> "screenshot".
QString screenshotBaseName(const QString& worldName, const QDateTime& when);
// `.screenshot auto` writes dir/<screenshotBaseName>.png. If that file exists (repeated shots within one second),
// " 2", " 3", ... is appended before the extension rather than overwriting.
QString screenshotAutoPath(const QString& dir, const QString& worldName, const QDateTime& when);
// The dialog's chosen name, with ".png" appended when none was typed.
QString withPngSuffix(const QString& path);
// Writes the map frame (the MapWidget's native image) as an opaque PNG, creating the directory. Returns false and sets *err on failure.
bool saveScreenshot(const QImage& frame, const QString& path, QString* err);

}  // namespace fugue::shell
