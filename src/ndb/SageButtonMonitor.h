#ifndef SAGE_BUTTON_MONITOR_H
#define SAGE_BUTTON_MONITOR_H

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QString>

class QSocketNotifier;

namespace NDB {

// Keeps the Sage gpio-keys evdev device open and drains its private event queue.
//
// The diagnostic build showed that simply keeping this read-only descriptor
// open is enough to avoid the first page-button press being lost after idle on
// the validated Sage firmware. There is intentionally no polling, event filter,
// input injection, timer, or continuous storage I/O here.
class SageButtonMonitor : public QObject {
    Q_OBJECT

public:
    explicit SageButtonMonitor(QObject *parent = nullptr);
    ~SageButtonMonitor();

    bool start();
    QString diagnostics() const;

public Q_SLOTS:
    // Kept for compatibility with the diagnostic D-Bus surface. They no longer
    // collect per-page/per-view data, so normal reading has no string/log churn.
    void clearDiagnostics();
    void recordPageChanged(int pageNum);
    void recordViewChanged(QString const& viewName);

private Q_SLOTS:
    void readInputEvents(int fd);

private:
    struct InputDevice {
        int fd;
        QString path;
        QString name;
        QSocketNotifier *notifier;
    };

    InputDevice *deviceForFd(int fd);

    QList<InputDevice> inputDevices;
    QElapsedTimer elapsed;
    quint64 keyEventsDrained = 0;
    quint64 readErrors = 0;
    bool started = false;
};

} // namespace NDB

#endif // SAGE_BUTTON_MONITOR_H
