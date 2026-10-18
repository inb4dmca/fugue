// One pager per user: a second `fugue-qt pager [link]` hands its link, or a request to show the window, to the pager already
// running and exits. The channel is a local socket in the user's runtime directory (Flatpak: the app's shared one), named
// after the Fugue state directory, so a run with another HOME is its own pager.
#pragma once
#include <QObject>
#include <QString>
#include <functional>

class QLocalServer;

namespace fugue::pager {

QString instanceName();
// Sends `link` ("" = show the window) to the pager listening on `name`. True once that pager has taken it.
bool forwardToRunning(const QString& name, const QString& link, int timeoutMs = 3000);

class InstanceServer : public QObject {
public:
    explicit InstanceServer(std::function<void(const QString& link)> onRequest, QObject* parent = nullptr);
    // False when another pager answers on `name`; a socket left behind by a pager that died is replaced.
    bool listen(const QString& name);

private:
    QLocalServer* server_;
    std::function<void(const QString&)> onRequest_;
};

}  // namespace fugue::pager
