#include "shell/screenshot.h"
#include <QDir>
#include <QFileInfo>
#include <QImageWriter>

namespace fugue::shell {
QString screenshotBaseName(const QString& worldName, const QDateTime& when) {
    static const QString bad = QStringLiteral("\\/:*?\"<>|");
    QString name;
    for (QChar c : worldName)
        if (!bad.contains(c) && c.unicode() >= 0x20) name += c;
    name = name.trimmed();
    if (name.isEmpty()) name = QStringLiteral("screenshot");
    return name + ' ' + when.toString(QStringLiteral("yyyy-MM-dd HHmmss"));
}

QString screenshotAutoPath(const QString& dir, const QString& worldName, const QDateTime& when) {
    const QString base = QDir(dir).filePath(screenshotBaseName(worldName, when));
    QString path = base + ".png";
    for (int i = 2; QFileInfo::exists(path); ++i) path = base + ' ' + QString::number(i) + ".png";
    return path;
}

QString withPngSuffix(const QString& path) {
    if (path.isEmpty() || !QFileInfo(path).suffix().isEmpty()) return path;
    return path + ".png";
}

bool saveScreenshot(const QImage& frame, const QString& path, QString* error) {
    if (error) error->clear();
    auto fail = [&](const QString& why) {
        if (error) *error = why;
        return false;
    };
    if (frame.isNull()) return fail(QStringLiteral("no map frame to capture"));
    if (path.isEmpty()) return fail(QStringLiteral("no screenshot destination"));
    const auto dir = QFileInfo(path).absolutePath();
    if (!QDir().mkpath(dir)) return fail(QStringLiteral("cannot create ") + dir);
    QImageWriter writer(path, "png");
    if (!writer.write(frame.convertToFormat(QImage::Format_RGB888))) return fail(writer.errorString());
    return true;
}
}  // namespace fugue::shell
