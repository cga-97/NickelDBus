include ../NickelHook/NickelHook.mk

override LIBRARY  := libsagebuttonsfix.so
override SOURCES  += src/sagebuttonsfix.c
override CFLAGS   += -Wall -Wextra -Werror
override CXXFLAGS += -Wall -Wextra -Werror -Wno-missing-field-initializers

override UNINSTALL_FILE := res/sagebuttonsfix
override GENERATED += $(UNINSTALL_FILE)
override KOBOROOT += $(UNINSTALL_FILE):/mnt/onboard/.adds/sagebuttonsfix

override GITIGNORE += *.o *.moc *.moc.o KoboRoot.tgz res/sagebuttonsfix

.PHONY: uninstall-file

uninstall-file:
	printf '%s\n' "$(VERSION)" > $(UNINSTALL_FILE)

all: uninstall-file

include ../NickelHook/NickelHook.mk
