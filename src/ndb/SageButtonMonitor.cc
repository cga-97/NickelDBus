#include "SageButtonMonitor.h"

#include <QDateTime>
#include <QSocketNotifier>

#include <NickelHook.h>

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace {

const int kMaximumInputDevices = 32;
const int kReadBatchSize = 16;

QString inputDevicePath(int index) {
    return QStringLiteral("/dev/input/event%1").arg(index);
}

bool isButtonDevice(QString const& name) {
    return name.contains(QStringLiteral("gpio-keys"), Qt::CaseInsensitive);
}

} // namespace

namespace NDB {

SageButtonMonitor::SageButtonMonitor(QObject *parent)
    : QObject(parent) {
    elapsed.start();
}

SageButtonMonitor::~SageButtonMonitor() {
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

    for (int index = 0; index < kMaximumInputDevices; ++index) {
        QString path = inputDevicePath(index);
        QByteArray encodedPath = path.toLocal8Bit();
        int fd = open(encodedPath.constData(), O_RDONLY | O_NONBLOCK);
        if (fd < 0) {
            if (errno != ENOENT) {
                nh_log("SageButtonsFix: unable to open %s: %s",
                    encodedPath.constData(), strerror(errno));
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

        if (!isButtonDevice(name)) {
            close(fd);
            continue;
        }

        QSocketNotifier *notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
        if (!QObject::connect(notifier, SIGNAL(activated(int)), this, SLOT(readInputEvents(int)))) {
            nh_log("SageButtonsFix: unable to watch %s (%s)",
                encodedPath.constData(), name.toLocal8Bit().constData());
            delete notifier;
            close(fd);
            continue;
        }

        InputDevice device = {fd, path, name, notifier};
        inputDevices.append(device);
        nh_log("SageButtonsFix: keeping %s (%s) open",
            encodedPath.constData(), name.toLocal8Bit().constData());
    }

    if (inputDevices.isEmpty()) {
        nh_log("SageButtonsFix: no gpio-keys evdev device found");
        return false;
    }

    nh_log("SageButtonsFix: active on %d gpio-keys device(s), no polling",
        inputDevices.size());
    return true;
}

QString SageButtonMonitor::diagnostics() const {
    QString report;
    report.append(QStringLiteral("SageButtonsFix keepalive status\n"));
    report.append(QStringLiteral("generated_utc=%1\n")
        .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));
    report.append(QStringLiteral("monitor_uptime_ms=%1\n")
        .arg(elapsed.isValid() ? elapsed.elapsed() : 0));
    report.append(QStringLiteral("mode=read-only gpio-keys keepalive; no polling\n"));
    report.append(QStringLiteral("evdev_devices=%1\n").arg(inputDevices.size()));
    report.append(QStringLiteral("key_events_drained=%1\n").arg(keyEventsDrained));
    report.append(QStringLiteral("read_errors=%1\n").arg(readErrors));

    for (int i = 0; i < inputDevices.size(); ++i) {
        InputDevice const& device = inputDevices.at(i);
        report.append(QStringLiteral("device_%1=%2 | %3 | notifier=%4\n")
            .arg(i)
            .arg(device.path, device.name)
            .arg(device.notifier->isEnabled() ? QStringLiteral("enabled")
                                              : QStringLiteral("disabled")));
    }

    return report;
}

void SageButtonMonitor::clearDiagnostics() {
    keyEventsDrained = 0;
    readErrors = 0;
}

void SageButtonMonitor::recordPageChanged(int pageNum) {
    Q_UNUSED(pageNum);
}

void SageButtonMonitor::recordViewChanged(QString const& viewName) {
    Q_UNUSED(viewName);
}

void SageButtonMonitor::readInputEvents(int fd) {
    InputDevice *device = deviceForFd(fd);
    if (!device) {
        nh_log("SageButtonsFix: activation for unknown fd %d", fd);
        return;
    }

    struct input_event events[kReadBatchSize];

    for (;;) {
        ssize_t bytesRead = read(fd, events, sizeof(events));
        if (bytesRead > 0) {
            const int eventCount = static_cast<int>(bytesRead / sizeof(struct input_event));
            for (int i = 0; i < eventCount; ++i) {
                if (events[i].type == EV_KEY) {
                    ++keyEventsDrained;
                }
            }
            continue;
        }

        if (bytesRead == 0) {
            break;
        }

        const int readError = errno;
        if (readError == EAGAIN || readError == EWOULDBLOCK || readError == EINTR) {
            break;
        }

        ++readErrors;
        device->notifier->setEnabled(false);
        QByteArray encodedPath = device->path.toLocal8Bit();
        nh_log("SageButtonsFix: disabling notifier for %s after read error %d",
            encodedPath.constData(), readError);
        break;
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

} // namespace NDB
