#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
task_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
if [[ "${EUID}" -eq 0 ]]; then
    echo "Run this installer as your normal desktop user, not root." >&2
    exit 1
fi
if [[ ! -x /usr/bin/python3 ]]; then
    if [[ "${1:-}" == "--check" ]]; then
        echo "Python is missing. Install with: sudo pacman -Syu --needed python" >&2
        exit 1
    fi
    if ! command -v pacman >/dev/null 2>&1; then
        echo "This installer requires Arch Linux/CachyOS and pacman." >&2
        exit 1
    fi
    # shellcheck source=/etc/os-release
    source /etc/os-release
    if [[ "${ID:-}" != "arch" && "${ID:-}" != "cachyos" && " ${ID_LIKE:-} " != *" arch "* ]]; then
        echo "Unsupported distribution." >&2
        exit 1
    fi
    echo "Python is required. Proposed command: sudo pacman -Syu --needed python"
    if [[ ! -t 0 ]]; then exit 1; fi
    read -r -p "Run this command? [y/N] " task_answer
    case "${task_answer}" in y|Y|yes|YES) sudo pacman -Syu --needed python ;; *) exit 1 ;; esac
fi
exec /usr/bin/python3 -B "$task_dir/packaging/install.py" "$@"
