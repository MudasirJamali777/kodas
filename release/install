#!/bin/sh
# Kodas language installer
# Usage:  curl -fsSL https://kodas.xo.je/install | sh
set -eu

KODAS_VERSION="${KODAS_VERSION:-0.16.0}"
BINARY_NAME="kodas"
KODAS_BASE_URL="${KODAS_BASE_URL:-https://kodas.xo.je}"

log()  { printf '\033[1;36m==>\033[0m %s\n' "$*" >&2; }
warn() { printf '\033[1;33m!!\033[0m  %s\n' "$*" >&2; }
err()  { printf '\033[1;31merror:\033[0m %s\n' "$*" >&2; }

main() {
    platform="$(uname -s | tr '[:upper:]' '[:lower:]')"
    arch="$(uname -m)"
    case "$platform" in
        linux)  plat_dir="linux-x86_64"; bin_name="kodas" ;;
        darwin)
            case "$arch" in
                arm64) plat_dir="macos-arm64" ;;
                x86_64) plat_dir="macos-x86_64" ;;
                *) err "unsupported macOS arch: $arch"; exit 1 ;;
            esac
            bin_name="kodas" ;;
        msys*|mingw*|cygwin*)
            err "Windows: download kodas-windows-x86_64.zip and run install.bat. WSL works via this script."; exit 1 ;;
        *) err "unsupported platform: $platform (linux-x86_64 today)"; exit 1 ;;
    esac
    case "$arch" in
        x86_64|amd64) : ;;
        aarch64|arm64) [ "$platform" = "darwin" ] || { err "linux-arm64 coming soon"; exit 1; } ;;
        *) err "unsupported architecture: $arch"; exit 1 ;;
    esac

    # Install prefix: /usr/local if writable, else ~/.local
    prefix=""
    if [ -n "${KODAS_PREFIX:-}" ]; then prefix="$KODAS_PREFIX"
    elif [ -d /usr/local/bin ] && [ -w /usr/local/bin ]; then prefix="/usr/local"
    else
        prefix="$HOME/.local"
        [ -d "$HOME/.kodas" ] && prefix="$HOME/.kodas"
    fi
    mkdir -p "$prefix/bin" "$prefix/lib/kodas"

    zip_url="$KODAS_BASE_URL/kodas-$plat_dir.zip"
    tmpdir="$(mktemp -d)"; trap 'rm -rf "$tmpdir"' EXIT
    zip_path="$tmpdir/kodas.zip"

    log "Downloading Kodas v$KODAS_VERSION ($plat_dir)"
    echo "    $zip_url" >&2
    if command -v curl >/dev/null 2>&1; then curl -fsSL --retry 2 "$zip_url" -o "$zip_path"
    elif command -v wget >/dev/null 2>&1; then wget -q "$zip_url" -O "$zip_path"
    else err "need curl or wget"; exit 1; fi

    if ! unzip -tq "$zip_path" >/dev/null 2>&1; then
        err "download was not a valid zip"; exit 1; fi

    log "Extracting..."
    unzip -q "$zip_path" -d "$tmpdir"

    bin_src="$(find "$tmpdir" -maxdepth 4 -name "$bin_name" -type f | head -1)"
    [ -z "$bin_src" ] && { err "could not find $bin_name binary"; exit 1; }

    # Locate lib/kodas directory (containing krt.c, stdlib/)
    lib_src=""
    for c in "$(dirname "$bin_src")/../lib/kodas" "$(dirname "$bin_src")/lib/kodas" "$(dirname "$bin_src")"; do
        if [ -f "$c/krt.c" ] && [ -d "$c/stdlib" ]; then lib_src="$c"; break; fi
    done

    log "Installing binary -> $prefix/bin/$bin_name"
    install -m 0755 "$bin_src" "$prefix/bin/$bin_name" 2>/dev/null || { cp "$bin_src" "$prefix/bin/$bin_name"; chmod 0755 "$prefix/bin/$bin_name"; }

    if [ -n "$lib_src" ]; then
        log "Installing stdlib + runtime -> $prefix/lib/kodas/"
        cp -R "$lib_src/." "$prefix/lib/kodas/"
    fi

    echo "" >&2
    printf "  \033[1;32mKodas v%s installed!\033[0m\n" "$KODAS_VERSION" >&2
    echo "    bin: $prefix/bin/$bin_name" >&2
    echo "    lib: $prefix/lib/kodas/" >&2
    echo "" >&2

    if command -v "$BINARY_NAME" >/dev/null 2>&1; then
        echo "  Try:  kodas run file.kds" >&2
        echo "        kodas build file.kds" >&2
    else
        echo "  Add $prefix/bin to your PATH:" >&2
        case "${SHELL:-}" in
            */zsh)  echo "    echo 'export PATH=\"$prefix/bin:\$PATH\"' >> ~/.zshrc && source ~/.zshrc" >&2 ;;
            */bash) echo "    echo 'export PATH=\"$prefix/bin:\$PATH\"' >> ~/.bashrc && source ~/.bashrc" >&2 ;;
            */fish) echo "    fish_add_path $prefix/bin" >&2 ;;
            *)      echo "    export PATH=\"$prefix/bin:\$PATH\"" >&2 ;;
        esac
    fi
}
main "$@"
