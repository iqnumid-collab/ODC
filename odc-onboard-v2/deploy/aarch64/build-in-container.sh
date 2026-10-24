#!/usr/bin/env bash
# Cross-compile odc-onboard-v2 inside an amd64 Debian container → aarch64 binaries.
# Invoked by scripts/docker-build-onboard-aarch64.sh via `docker run` (not buildx),
# because buildx+apt hits an infinite "Tried to start delayed item" loop in this lab.
set -euo pipefail

export DEBIAN_FRONTEND=noninteractive
export HOME=/root
export CPM_SOURCE_CACHE=/root/.cache/CPM

SRC="${SRC:-/src}"
OUT="${OUT:-/out}"

apt_cfg() {
  printf '%s\n' \
    'Acquire::Retries "8";' \
    'Acquire::http::Pipeline-Depth "0";' \
    'Acquire::https::Pipeline-Depth "0";' \
    'Acquire::Queue-Mode "access";' \
    'Acquire::Languages "none";' \
    'Acquire::https::Verify-Peer "false";' \
    'Acquire::https::Verify-Host "false";' \
    'APT::Install-Recommends "false";' \
    > /etc/apt/apt.conf.d/99odc
  sed -i 's|http://|https://|g' /etc/apt/sources.list.d/debian.sources 2>/dev/null || true
  sed -i 's|http://|https://|g' /etc/apt/sources.list 2>/dev/null || true
}

apt_retry() {
  local n=1
  until "$@"; do
    if [[ "$n" -ge 6 ]]; then
      echo "ERROR: apt command failed after $n attempts: $*" >&2
      return 1
    fi
    echo "==> apt retry $n: $*" >&2
    sleep $((n * 2))
    n=$((n + 1))
    apt-get update || true
  done
}

echo "==> apt host toolchain"
apt_cfg
apt_retry apt-get update
# Install in small batches to avoid apt delayed-item hangs on large transactions.
apt_retry apt-get install -y --no-install-recommends ca-certificates
apt_retry apt-get install -y --no-install-recommends cmake ninja-build pkg-config git curl file
apt_retry apt-get install -y --no-install-recommends gcc-aarch64-linux-gnu g++-aarch64-linux-gnu
apt_retry apt-get install -y --no-install-recommends nlohmann-json3-dev libcli11-dev || \
  apt_retry apt-get install -y --no-install-recommends nlohmann-json3-dev

echo "==> arm64 sysroot (download + extract, no dpkg configure / no qemu)"
dpkg --add-architecture arm64
apt_retry apt-get update
mkdir -p /tmp/arm64-debs /sysroot
cd /tmp/arm64-debs
# --download-only still resolves qemu into the archive set, but does not configure it.
apt_retry apt-get install -y --download-only --no-install-recommends \
  libc6:arm64 \
  libgcc-s1:arm64 \
  libstdc++6:arm64 \
  libssl-dev:arm64 \
  libspdlog-dev:arm64 \
  libfmt-dev:arm64 \
  libsodium-dev:arm64 \
  libglib2.0-dev:arm64 \
  libgudev-1.0-dev:arm64 \
  libudev-dev:arm64 \
  libqmi-glib-dev:arm64 \
  libmbim-glib-dev:arm64 \
  libqrtr-glib-dev:arm64 \
  libgstreamer1.0-dev:arm64 \
  libgstreamer-plugins-base1.0-dev:arm64 \
  libgstrtspserver-1.0-dev:arm64

# Extract only arm64 + arch-independent debs into sysroot.
while IFS= read -r -d '' deb; do
  base="$(basename "$deb")"
  case "$base" in
    *_amd64.deb|qemu-*.deb) continue ;;
  esac
  dpkg-deb -x "$deb" /sysroot
done < <(find /var/cache/apt/archives /tmp/arm64-debs -maxdepth 1 -name '*.deb' -print0)

test -f /sysroot/usr/lib/aarch64-linux-gnu/pkgconfig/qmi-glib.pc
test -d /sysroot/usr/include/libqmi-glib

# Debian merged-/usr: linker scripts reference /lib/aarch64-linux-gnu/... inside sysroot.
if [[ ! -e /sysroot/lib ]]; then
  ln -s usr/lib /sysroot/lib
fi
if [[ ! -e /sysroot/lib64 ]]; then
  ln -s usr/lib /sysroot/lib64
fi
# Ensure libc runtime libs landed (needed by cross ld scripts).
if [[ ! -e /sysroot/lib/aarch64-linux-gnu/libm.so.6 ]]; then
  apt-get download libc6:arm64
  dpkg-deb -x libc6_*.deb /sysroot || dpkg-deb -x ./libc6*.deb /sysroot
  [[ -e /sysroot/lib ]] || ln -s usr/lib /sysroot/lib
fi
test -e /sysroot/lib/aarch64-linux-gnu/libm.so.6

# CMAKE_SYSROOT hides host /usr/include; mirror header-only host packages into sysroot.
mkdir -p /sysroot/usr/include /sysroot/usr/share/cmake /sysroot/usr/lib/cmake
for d in nlohmann CLI; do
  if [[ -d "/usr/include/$d" ]]; then
    cp -a "/usr/include/$d" /sysroot/usr/include/
  fi
done
for d in nlohmann_json CLI11; do
  if [[ -d "/usr/share/cmake/$d" ]]; then
    cp -a "/usr/share/cmake/$d" /sysroot/usr/share/cmake/
  fi
  if [[ -d "/usr/lib/cmake/$d" ]]; then
    cp -a "/usr/lib/cmake/$d" /sysroot/usr/lib/cmake/
  fi
done
test -f /sysroot/usr/include/nlohmann/json.hpp

cat > /opt/aarch64-toolchain.cmake <<'EOF'
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
set(CMAKE_SYSROOT /sysroot)
set(CMAKE_FIND_ROOT_PATH /sysroot /usr/aarch64-linux-gnu)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
set(PKG_CONFIG_EXECUTABLE /usr/bin/pkg-config CACHE FILEPATH "")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "/sysroot")
set(ENV{PKG_CONFIG_LIBDIR} "/sysroot/usr/lib/aarch64-linux-gnu/pkgconfig:/sysroot/usr/share/pkgconfig")
set(OpenSSL_ROOT /sysroot/usr)
# Prefer shared libs under sysroot; avoid picking stray absolute host paths.
link_directories(/sysroot/usr/lib/aarch64-linux-gnu)
set(CMAKE_EXE_LINKER_FLAGS_INIT "-L/sysroot/usr/lib/aarch64-linux-gnu")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-L/sysroot/usr/lib/aarch64-linux-gnu")
EOF

export PKG_CONFIG_SYSROOT_DIR=/sysroot
export PKG_CONFIG_LIBDIR=/sysroot/usr/lib/aarch64-linux-gnu/pkgconfig:/sysroot/usr/share/pkgconfig

echo "==> cmake configure + build"
cmake -B /build -S "$SRC" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=/opt/aarch64-toolchain.cmake \
  -G Ninja
cmake --build /build -j"$(nproc)"

cmake -B /build-vs -S "$SRC/tools/odc-video-source" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=/opt/aarch64-toolchain.cmake \
  -G Ninja
cmake --build /build-vs -j"$(nproc)"

cmake -B /build-mon -S "$SRC/tools/odc-rtsp-monitor" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=/opt/aarch64-toolchain.cmake \
  -G Ninja
cmake --build /build-mon -j"$(nproc)"

echo "==> stage package -> $OUT"
mkdir -p "$OUT/tools" "$OUT/config/vpn"
cp /build/odc-onboard-v2 "$OUT/"
ln -sfn odc-onboard-v2 "$OUT/odc-cli"
cp /build-vs/odc-video-source "$OUT/tools/"
cp /build-mon/odc-rtsp-monitor "$OUT/tools/"
cp "$SRC/config/system.example.yaml" "$OUT/config/system.yaml.template"
cp "$SRC/deploy/aarch64/install.sh" "$OUT/install.sh"
cp "$SRC/deploy/aarch64/odc-onboard-v2.service" "$OUT/config/odc-onboard-v2.service"
cp "$SRC/deploy/aarch64/odc-onboard-v2.service" "$OUT/odc-onboard-v2.service"
if [[ -d "$SRC/configs" ]]; then
  cp -a "$SRC/configs/." "$OUT/config/vpn/" 2>/dev/null || true
fi
chmod +x "$OUT/install.sh" "$OUT/odc-onboard-v2" "$OUT/tools/"*
if [[ -n "${HOST_UID:-}" && -n "${HOST_GID:-}" ]]; then
  chown -R "${HOST_UID}:${HOST_GID}" "$OUT" || true
fi
file "$OUT/odc-onboard-v2"
ls -la "$OUT"
echo "OK: package staged at $OUT"
