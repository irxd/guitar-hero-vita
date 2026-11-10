#!/usr/bin/env bash
# Installs the PS Vita toolchain inside WSL/Ubuntu (run as root):
#   VitaSDK (bootstrap from vdpm, follows the "supported" release channel)
#   + the packages gd01 needs (SDL3, vitaGL and its deps, zlib, plugin libs).
# Idempotent: re-running skips finished steps.
#
# Usage (from PowerShell, not Git Bash - Git Bash rewrites /mnt/... paths):
#   wsl -d Ubuntu -u root -- bash /mnt/d/Personal/guitar-hero-vita/tools/setup-vitasdk.sh
#
# Note: vdpm was rewritten (pacman client in $VITASDK/libexec/vdpm, signed
# release channels). The repo's install-all.sh still calls the old shell
# client and fails ("package client is not executable"), so we call the
# installed `vdpm` binary directly.
set -euo pipefail

export VITASDK="${VITASDK:-/usr/local/vitasdk}"
export PATH="$VITASDK/bin:$PATH"
export VDPM_NONINTERACTIVE=1
WORK="${WORK:-/root/vita-src}"

PACKAGES=(
    sdl3          # window, input, audio (gd01 is SDL3-based)
    vitaGL        # OpenGL on sceGxm (needs libshacccg.suprx on the console)
    vitaShaRK     # vitaGL runtime shader compiler front-end
    libmathneon   # vitaGL dependency
    zlib
    taihen
    kubridge
)

mkdir -p "$WORK"
log() { printf '\n=== %s\n' "$*"; }

# 1. Environment for future shells
if [ ! -f /etc/profile.d/vitasdk.sh ]; then
    log "Writing /etc/profile.d/vitasdk.sh"
    cat > /etc/profile.d/vitasdk.sh <<EOF
export VITASDK=$VITASDK
export PATH=\$VITASDK/bin:\$PATH
EOF
fi

# 2. VitaSDK toolchain
if [ ! -x "$VITASDK/bin/arm-vita-eabi-gcc" ]; then
    log "Bootstrapping VitaSDK into $VITASDK"
    rm -rf "$WORK/vdpm"
    git clone --depth 1 https://github.com/vitasdk/vdpm "$WORK/vdpm"
    (cd "$WORK/vdpm" && ./bootstrap-vitasdk.sh)
else
    log "VitaSDK already installed"
fi
vdpm status
arm-vita-eabi-gcc --version | head -1

# 3. Packages (--needed semantics: already-installed ones are skipped)
log "Installing packages: ${PACKAGES[*]}"
vdpm install "${PACKAGES[@]}"

# 4. Report
log "Installed packages"
vdpm list "${PACKAGES[@]}"
log "Done"
