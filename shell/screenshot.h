// Native MAP screenshots: the same opaque PNG for dialog and automatic saves.
// Automatic names drop Windows-reserved/control characters from world.name and
// append local time. Repeated captures within a second get numeric suffixes.
#pragma once
#include <QDateTime>
#include <QImage>
#include <QString>

namespace fugue::shell {
QString screenshotBaseName(const QString& worldName, const QDateTime& when);
QString screenshotAutoPath(const QString& dir, const QString& worldName, const QDateTime& when);
QString withPngSuffix(const QString& path);
// Creates the destination directory; reports null frames and write failures.
bool saveScreenshot(const QImage& frame, const QString& path, QString* error = nullptr);
}  // namespace fugue::shell
