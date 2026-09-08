#!/bin/sh
# extract.sh <file> <funcname> - print one function from a dumpbin -DISASM listing
awk -v f="$2" '
  /^[A-Za-z_?][A-Za-z0-9_?@$]*:/ { infn = ($0 == f ":") ; if (infn) { print; next } }
  infn { print }
' "$1"
