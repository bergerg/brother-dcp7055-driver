#!/bin/sh
# Run with sudo. Installs the filter and creates the print queue.
set -e
cd "$(dirname "$0")"
[ "$(id -u)" = 0 ] || { echo "run with sudo"; exit 1; }
URI=$(lpinfo -v | awk '$2 ~ /^usb:\/\/Brother\/DCP-7055\?/ {print $2; exit}')
[ -n "$URI" ] || { echo "DCP-7055 not found on USB"; exit 1; }
D=/Library/Printers/BrotherDCP7055
install -d -o root -g wheel -m 755 "$D"
install -o root -g wheel -m 755 rastertodcp7055 "$D/rastertodcp7055"
lpadmin -p Brother_DCP_7055 -E -v "$URI" -P brother-dcp7055.ppd -D "Brother DCP-7055" -o printer-is-shared=false
lpadmin -d Brother_DCP_7055
echo "Installed. Queue: Brother_DCP_7055"
