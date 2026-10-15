#!/bin/sh
# Kodas one-line installer for Linux/macOS
# Usage: curl -fsSL https://raw.githubusercontent.com/MudasirJamali777/kodas/main/install.sh | sh

set -e

REPO="MudasirJamali777/kodas"
INSTALL_DIR="${INSTALL_DIR:-$HOME/.local/bin}"

echo "Installing Kodas..."

# Detect OS
OS=$(uname -s | tr '[:upper:]' '[:lower:]')
ARCH=$(uname -m)

# For now, build from source
if ! command -v gcc >/dev/null 2>&1 && ! command -v clang >/dev/null 2>&1; then
    echo "Error: gcc or clang is required to build Kodas"
    exit 1
fi

TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

cd "$TMPDIR"
echo "  Downloading source..."
if command -v git >/dev/null 2>&1; then
    git clone --depth 1 "https://github.com/${REPO}.git" kodas 2>/dev/null || \
        curl -fsSL "https://github.com/${REPO}/archive/main.tar.gz" | tar xz --strip=1
else
    curl -fsSL "https://github.com/${REPO}/archive/main.tar.gz" | tar xz --strip=1
fi

cd seed
echo "  Building..."
if command -v gcc >/dev/null 2>&1; then
    gcc -I src -std=c11 -O2 -D_POSIX_C_SOURCE=200809L src/*.c -o kodas -lm
else
    clang -I src -std=c11 -O2 src/*.c -o kodas -lm
fi

mkdir -p "$INSTALL_DIR"
cp kodas "$INSTALL_DIR/kodas"
chmod +x "$INSTALL_DIR/kodas"

echo ""
echo "  ✓ Kodas installed to $INSTALL_DIR/kodas"
echo ""
echo "Make sure $INSTALL_DIR is in your PATH:"
echo "  export PATH=\"\$HOME/.local/bin:\$PATH\""
echo ""
echo "Get started:"
echo "  kodas repl"
echo "  echo 'show \"Hello, Kodas!\"' > hello.kds && kodas run hello.kds"
