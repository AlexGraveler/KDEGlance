#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
task_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
if [[ "${EUID}" -eq 0 ]]; then echo "Run as your normal desktop user, not root." >&2; exit 1; fi
if [[ ! -x /usr/bin/python3 ]]; then echo "Python 3 is required for safe manifest-based removal." >&2; exit 1; fi
exec /usr/bin/python3 -B "$task_dir/packaging/uninstall.py" "$@"
