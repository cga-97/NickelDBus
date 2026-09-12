#include "SageButtonMonitor.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QEvent>
#include <QKeyEvent>
#include <QSocketNotifier>

#include <NickelHook.h>

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace {

const int kMaximumInputDevices = 32;
const int kMaximumRecordedEvents = 256;

QString inputDevicePath(int index) {
    return QStringLiteral("/dev/input/event%1").arg(index);
}

QString eventTypeName(QEvent::Type type) {
    switch (type) {
    case QEvent::KeyPress:
        return QStringLiteral("press");
    case QEvent::KeyRelease:
        return QStringLiteral("release");
    default:
        return QStringLiteral("unknown");
    }
}

} // namespace

namespace NDB {

SageButtonMonitor::SageButtonMonitor(QObject *parent)
    : QObject(parent), started(false) {
    elapsed.start();
}

SageButtonMonitor::~SageButtonMonitor() {
    if (QCoreApplication::instance()) {
        QCoreApplication::instance()->removeEventFilter(this);
    }

    for (int i = 0; i < inputDevices.size(); ++i) {
        inputDevices[i].notifier->setEnabled(false);
        close(inputDevices[i].fd);
    }
}

bool SageButtonMonitor::start() {
    if (started) {
        return !inputDevices.isEmpty();
    }
    started = true;

    QCoreApplication *application = QCoreApplication::instance();
    if (application) {
        application->installEventFilter(this);
        startupNotes.append(QStringLiteral("Qt application event filter installed"));
    } else {
        startupNotes.append(QStringLiteral("ERROR: no QCoreApplication instance"));
    }

    for (int index = 0; index < kMaximumInputDevices; ++index) {
        QString path = inputDevicePath(index);
        QByteArray encodedPath = path.toLocal8Bit();
        int fd = open(encodedPath.constData(), O_RDONLY | O_NONBLOCK);
        if (fd < 0) {
            if (errno != ENOENT) {
                startupNotes.append(QStringLiteral("Unable to open %1: %2")
                    .arg(path, QString::fromLocal8Bit(strerror(errno))));
            }
            continue;
        }

        int descriptorFlags = fcntl(fd, F_GETFD);
        if (descriptorFlags >= 0) {
            fcntl(fd, F_SETFD, descriptorFlags | FD_CLOEXEC);
        }

        char deviceName[256] = {0};
        QString name = QStringLiteral("unknown");
        if (ioctl(fd, EVIOCGNAME(sizeof(deviceName)), deviceName) >= 0) {
            name = QString::fromLocal8Bit(deviceName);
        }

        QSocketNotifier *notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
        if (!QObject::connect(notifier, SIGNAL(activated(int)), this, SLOT(readInputEvents(int)))) {
            startupNotes.append(QStringLiteral("Unable to watch %1 (%2)").arg(path, name));
            delete notifier;
            close(fd);
            continue;
        }

        InputDevice device = {fd, path, name, notifier};
        inputDevices.append(device);
        startupNotes.append(QStringLiteral("Watching %1 (%2)").arg(path, name));
    }

    appendEvent(QStringLiteral("monitor-start devices=%1").arg(inputDevices.size()));
    nh_log("SageButtonMonitor: watching %d evdev device(s)", inputDevices.size());
    return !inputDevices.isEmpty();
}

QString SageButtonMonitor::diagnostics() const {
    QString report;
    report.append(QStringLiteral("SageButtonsFix passive diagnostics\n"));
    report.append(QStringLiteral("generated_utc=%1\n")
        .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));
    report.append(QStringLiteral("monitor_uptime_ms=%1\n")
        .arg(elapsed.isValid() ? elapsed.elapsed() : 0));
    report.append(QStringLiteral("evdev_devices=%1\n\n")
        .arg(inputDevices.size()));

    report.append(QStringLiteral("[startup]\n"));
    for (int i = 0; i < startupNotes.size(); ++i) {
        report.append(startupNotes.at(i));
        report.append(QLatin1Char('\n'));
    }

    report.append(QStringLiteral("\n[events]\n"));
    for (int i = 0; i < recentEvents.size(); ++i) {
        report.append(recentEvents.at(i));
        report.append(QLatin1Char('\n'));
    }
    return report;
}

void SageButtonMonitor::clearDiagnostics() {
    recentEvents.clear();
    appendEvent(QStringLiteral("diagnostics-cleared"));
}

void SageButtonMonitor::recordPageChanged(int pageNum) {
    appendEvent(QStringLiteral("nickel page-changed page=%1").arg(pageNum));
}

void SageButtonMonitor::recordViewChanged(QString const& viewName) {
    appendEvent(QStringLiteral("nickel view-changed view=%1").arg(viewName));
}

bool SageButtonMonitor::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent*>(event);
        appendEvent(QStringLiteral("qt-key %1 key=%2 native-scan=%3 native-virtual=%4 autorepeat=%5 receiver=%6")
            .arg(eventTypeName(event->type()))
            .arg(keyEvent->key())
            .arg(keyEvent->nativeScanCode())
            .arg(keyEvent->nativeVirtualKey())
            .arg(keyEvent->isAutoRepeat() ? 1 : 0)
            .arg(objectDescription(watched)));
    } else if (event->type() == QEvent::ApplicationActivate) {
        appendEvent(QStringLiteral("qt application-activate"));
    } else if (event->type() == QEvent::ApplicationDeactivate) {
        appendEvent(QStringLiteral("qt application-deactivate"));
    }

    return QObject::eventFilter(watched, event);
}

void SageButtonMonitor::readInputEvents(int fd) {
    InputDevice *device = deviceForFd(fd);
    if (!device) {
        nh_log("SageButtonMonitor: activation for unknown fd %d", fd);
        return;
    }

    struct input_event inputEvent;
    ssize_t bytesRead;
    while ((bytesRead = read(fd, &inputEvent, sizeof(inputEvent))) == static_cast<ssize_t>(sizeof(inputEvent))) {
        if (inputEvent.type == EV_KEY) {
            appendEvent(QStringLiteral("evdev-key device=%1 name=%2 code=%3 value=%4")
                .arg(device->path, device->name)
                .arg(inputEvent.code)
                .arg(inputEvent.value));
        }
    }

    const int readError = errno;
    if (bytesRead < 0 && readError != EAGAIN && readError != EWOULDBLOCK && readError != EINTR) {
        appendEvent(QStringLiteral("evdev-error device=%1 error=%2")
            .arg(device->path, QString::fromLocal8Bit(strerror(readError))));
        device->notifier->setEnabled(false);
        QByteArray encodedPath = device->path.toLocal8Bit();
        nh_log("SageButtonMonitor: disabling %s after read error %d",
            encodedPath.constData(), readError);
    }
}

void SageButtonMonitor::appendEvent(QString const& event) {
    recentEvents.append(QStringLiteral("%1ms %2").arg(elapsed.elapsed()).arg(event));
    while (recentEvents.size() > kMaximumRecordedEvents) {
        recentEvents.removeFirst();
    }
}

SageButtonMonitor::InputDevice *SageButtonMonitor::deviceForFd(int fd) {
    for (int i = 0; i < inputDevices.size(); ++i) {
        if (inputDevices[i].fd == fd) {
            return &inputDevices[i];
        }
    }
    return nullptr;
}

QString SageButtonMonitor::objectDescription(QObject *object) const {
    if (!object) {
        return QStringLiteral("null");
    }

    QString className = QString::fromLatin1(object->metaObject()->className());
    if (object->objectName().isEmpty()) {
        return className;
    }
    return QStringLiteral("%1/%2").arg(className, object->objectName());
}

} // namespace NDB
