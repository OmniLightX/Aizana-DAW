#!/bin/bash
# Duenne Huelle um scripts/uitest.py — /bin/bash ist auf macOS 3.2 und kennt kein
# `coproc`; die unbenannte Pipe legt deshalb Python an (subprocess.Popen).
exec /usr/bin/env python3 "$(cd "$(dirname "$0")" && pwd)/uitest.py" "$@"
