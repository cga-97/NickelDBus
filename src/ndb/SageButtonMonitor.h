#ifndef SAGE_BUTTON_MONITOR_H
#define SAGE_BUTTON_MONITOR_H

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

class QEvent;
class QSocketNotifier;

namespace NDB {

class SageButtonMonitor : public QObject {
    Q_OBJECT

public:
    explicit SageButtonMonitor(QObject *parent = nullptr);
    ~SageButtonMonitor();

    bool start();
    QString diagnostics() const;

public Q_SLOTS:
    void clearDiagnostics();
    void recordPageChanged(int pageNum);
    void recordViewChanged(QString const& viewName);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private Q_SLOTS:
    void readInputEvents(int fd);

private:
    struct InputDevice {
        int fd;
        QString path;
        QString name;
        QSocketNotifier *notifier;
    };

    void appendEvent(QString const& event);
    InputDevice *deviceForFd(int fd);
    QString objectDescription(QObject *object) const;

    QList<InputDevice> inputDevices;
    QStringList recentEvents;
    QStringList startupNotes;
    QElapsedTimer elapsed;
    bool started;
};

} // namespace NDB

#endif // SAGE_BUTTON_MONITOR_H
