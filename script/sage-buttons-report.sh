#!/bin/sh

report="/mnt/onboard/sage-buttons-diagnostic.txt"
temporary="/mnt/onboard/.sage-buttons-diagnostic.$$"

cleanup() {
    rm -f "${temporary}"
}
trap cleanup EXIT
trap 'exit 1' HUP INT TERM

if /usr/bin/qndb -m sbfDiagnostics > "${temporary}"; then
    mv -f "${temporary}" "${report}"
    sync
    /usr/bin/qndb -m mwcToast 3000 "Sage Buttons" "Diagnostico guardado"
else
    /usr/bin/qndb -m mwcToast 5000 "Sage Buttons" "No se pudo guardar el diagnostico"
fi
