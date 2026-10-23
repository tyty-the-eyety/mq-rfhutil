#!/usr/bin/env bash
# Build the rfhutil-qt RPM and DEB packages.
#
# Dependencies
# ------------
# Fedora / RHEL:
#   sudo dnf install cmake gcc-c++ qt6-qtbase-devel rpm-build
#
# Ubuntu / Debian:
#   sudo apt install cmake g++ qt6-base-dev dpkg-dev
#
# Both distros also need the IBM MQ SDK (MQSeriesSDK package) installed
# at /opt/mqm (or pass MQ_HOME=<path> on the cmake line below).
#
# Usage
# -----
#   chmod +x build-packages.sh
#   ./build-packages.sh
#
# Output
# ------
#   rfhutil-qt-10.0.0.240-1.x86_64.rpm   (Fedora / RHEL)
#   rfhutil-qt_10.0.0.240_amd64.deb       (Ubuntu / Debian)
#
# The packages install the binary to /usr/bin/rfhutil-qt and add a
# desktop entry so the app appears in your application launcher.
# IBM MQ itself is loaded at runtime from /opt/mqm/lib64 (or wherever
# your MQ installation lives); it is not a package dependency.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build/linux-release"

echo "==> Configuring (Release)..."
cmake --preset linux-release -S "$SCRIPT_DIR"

echo "==> Building..."
cmake --build --preset linux-release -j"$(nproc)"

echo "==> Packaging RPM..."
cpack -G RPM --config "$BUILD_DIR/CPackConfig.cmake" -B "$SCRIPT_DIR"

echo "==> Packaging DEB..."
cpack -G DEB --config "$BUILD_DIR/CPackConfig.cmake" -B "$SCRIPT_DIR"

echo ""
echo "Packages written to $SCRIPT_DIR:"
ls -lh "$SCRIPT_DIR"/*.rpm "$SCRIPT_DIR"/*.deb 2>/dev/null || true
