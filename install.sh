#!/usr/bin/env bash
#
# One-shot build + install for codec_g729.so on the target Asterisk box.
#
# Usage:  ./install.sh
#
set -euo pipefail
cd "$(dirname "$0")"

say() { printf '\033[1;32m==>\033[0m %s\n' "$*"; }
die() { printf '\033[1;31mERROR:\033[0m %s\n' "$*" >&2; exit 1; }

# --- prerequisites --------------------------------------------------------
for tool in git cmake gcc make pkg-config; do
	command -v "$tool" >/dev/null 2>&1 || die "missing required tool: $tool"
done

# Asterisk headers must be present (asterisk-dev / asterisk-devel package).
if [ ! -f /usr/include/asterisk.h ] && [ ! -f /usr/include/asterisk/translate.h ]; then
	die "Asterisk development headers not found. Install 'asterisk-dev' (Debian/Ubuntu) or 'asterisk-devel' (RHEL)."
fi

# --- build ----------------------------------------------------------------
say "Fetching bcg729 submodule"
git submodule update --init --recursive

say "Building codec_g729.so"
make

# --- install --------------------------------------------------------------
say "Installing module"
if [ "$(id -u)" -eq 0 ]; then
	make install
else
	sudo make install
fi

cat <<'EOF'

Done. Load it from the Asterisk CLI:

    asterisk -rx "module load codec_g729.so"
    asterisk -rx "core show translation paths g729"

You should now see a real path, e.g.:

    g729 To ulaw : (g729@8000)->(slin@8000)->(ulaw@8000)
EOF
