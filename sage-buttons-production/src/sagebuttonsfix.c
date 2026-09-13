#include <NickelHook.h>

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static const int sage_buttons_maximum_input_devices = 32;
static const char sage_buttons_device_name[] = "gpio-keys";
static const char sage_buttons_install_file[] = "/mnt/onboard/.adds/sagebuttonsfix";

static int sage_buttons_fd = -1;

static int sage_buttons_init(void) {
    if (sage_buttons_fd >= 0) {
        return 0;
    }

    for (int index = 0; index < sage_buttons_maximum_input_devices; ++index) {
        char path[32];
        char device_name[256] = {0};
        int candidate;
        int descriptor_flags;

        snprintf(path, sizeof(path), "/dev/input/event%d", index);
        candidate = open(path, O_RDONLY | O_NONBLOCK);
        if (candidate < 0) {
            if (errno != ENOENT) {
                nh_log("unable to open %s: %m", path);
            }
            continue;
        }

        if (ioctl(candidate, EVIOCGNAME(sizeof(device_name)), device_name) < 0) {
            nh_log("unable to identify %s: %m", path);
            close(candidate);
            continue;
        }
        device_name[sizeof(device_name) - 1] = '\0';

        if (!strstr(device_name, sage_buttons_device_name)) {
            close(candidate);
            continue;
        }

        descriptor_flags = fcntl(candidate, F_GETFD);
        if (descriptor_flags >= 0) {
            fcntl(candidate, F_SETFD, descriptor_flags | FD_CLOEXEC);
        }

        sage_buttons_fd = candidate;
        nh_log("keeping %s open (%s)", path, device_name);
        return 0;
    }

    // This workaround is optional. Nickel must continue to start if the
    // device name changes on another firmware or model.
    nh_log("no gpio-keys evdev device found; workaround inactive");
    return 0;
}

static struct nh_info SageButtonsFixInfo = {
    .name            = "SageButtonsFix",
    .desc            = "Keeps the Kobo Sage gpio-keys evdev device open",
    .uninstall_xflag = sage_buttons_install_file,
};

// Use a plain file-scope initializer instead of NickelHook()'s compound
// literal. Older NickelTC ARM GCC versions reject that compound literal as a
// non-constant initializer for an object with static storage duration.
__attribute__((visibility("default")))
struct nh NickelHook = {
    .init  = sage_buttons_init,
    .info  = &SageButtonsFixInfo,
    .hook  = NULL,
    .dlsym = NULL,
};
