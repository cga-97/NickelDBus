# SageButtonsFix production candidate

This is the minimal standalone candidate for the Kobo Sage physical page-turn
button issue observed on firmware 4.38.23552.

It does only one thing: during Nickel startup it scans `/dev/input/event0` to
`event31`, identifies the device whose kernel name contains `gpio-keys`, and
keeps one read-only descriptor open for the lifetime of Nickel. It does not
read or inject input, install a Qt event filter, use D-Bus, resolve private
Nickel symbols, touch `Kobo eReader.conf`, or write to `/mnt/onboard` after
startup.

The descriptor is selected by device name rather than by event number because
event numbering is not a stable firmware ABI. If no matching device exists,
the mod logs the condition and remains inactive while Nickel continues to
start normally.

This is a hardware-test candidate, not yet a confirmed fix. The diagnostic
build used before this project kept a `gpio-keys` descriptor open and also
installed a notifier and instrumentation. The open-only variant therefore
needs an A/B test on the Kobo Sage before being treated as production.

Build from this directory with NickelTC:

```sh
make clean all koboroot
```

The resulting `KoboRoot.tgz` contains only the library and its NickelHook
installation marker.
