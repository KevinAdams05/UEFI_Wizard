#!/usr/bin/env bash
#
# build-cli.sh — cross-build just the CLI (uefi_wizard) on the build
# server. Used during Phase 3 development before the GUI sources land.
# Once App/MainWindow/DiskPicker are implemented, build-hpkg.sh handles
# both binaries and the .hpkg.
#
# Output: build/uefi_wizard
#
# Override paths via env vars:
#   HAIKU_BUILD   — top of the haiku source checkout (default: ~/haiku-build/haiku)
#   HAIKU_ARCH    — target arch (default: x86_64)

set -euo pipefail

HAIKU_BUILD="${HAIKU_BUILD:-$HOME/haiku-build/haiku}"
HAIKU_ARCH="${HAIKU_ARCH:-x86_64}"

GENERATED="$HAIKU_BUILD/generated.${HAIKU_ARCH}"
CROSS_BIN="$GENERATED/cross-tools-${HAIKU_ARCH}/bin"
CXX="$CROSS_BIN/${HAIKU_ARCH}-unknown-haiku-g++"

HAIKU_HEADERS="$HAIKU_BUILD/headers"
HAIKU_DEVEL_LIB="$GENERATED/objects/haiku/${HAIKU_ARCH}/packaging/packages_build/regular/hpkg_-haiku_devel.hpkg/contents/develop/lib"
HAIKU_RUNTIME_LIB="$GENERATED/objects/haiku/${HAIKU_ARCH}/packaging/packages_build/regular/hpkg_-haiku.hpkg/contents/lib"

GCC_SYSLIBS_DEVEL=$(ls -d "$GENERATED/build_packages"/gcc_syslibs_devel-*-"${HAIKU_ARCH}"/develop/lib 2>/dev/null | head -1)
GCC_SYSLIBS=$(ls -d "$GENERATED/build_packages"/gcc_syslibs-*-"${HAIKU_ARCH}"/lib 2>/dev/null | head -1)

if [ ! -x "$CXX" ]; then
	echo "ERROR: cross-compiler not found at $CXX" >&2
	exit 1
fi

PROJ="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$PROJ/src"
BUILD="$PROJ/build"

mkdir -p "$BUILD"

INCLUDES=(
	-I"$HAIKU_HEADERS"
	-I"$HAIKU_HEADERS/os"
	-I"$HAIKU_HEADERS/os/app"
	-I"$HAIKU_HEADERS/os/interface"
	-I"$HAIKU_HEADERS/os/storage"
	-I"$HAIKU_HEADERS/os/support"
	-I"$HAIKU_HEADERS/os/kernel"
	-I"$HAIKU_HEADERS/os/drivers"
	-I"$HAIKU_HEADERS/posix"
	-I"$HAIKU_HEADERS/config"
	-I"$HAIKU_HEADERS/private/storage"
	-I"$SRC"
)

CXXFLAGS=(-O0 -g -Wall -Wno-multichar -fno-strict-aliasing "${INCLUDES[@]}")

SOURCES=(
	src/Constants.cpp
	src/ArchUtils.cpp
	src/ESPManager.cpp
	src/LoaderInstaller.cpp
	src/cli/UEFIWizardCLI.cpp
)

OBJS=()
echo "==> Compiling..."
for s in "${SOURCES[@]}"; do
	obj="$BUILD/$(basename "${s%.cpp}").o"
	echo "    $s"
	"$CXX" "${CXXFLAGS[@]}" -c "$PROJ/$s" -o "$obj"
	OBJS+=("$obj")
done

echo "==> Linking uefi_wizard..."
"$CXX" "${OBJS[@]}" \
	-B"$HAIKU_DEVEL_LIB" \
	-L"$HAIKU_DEVEL_LIB" \
	-L"$HAIKU_RUNTIME_LIB" \
	-L"$GCC_SYSLIBS_DEVEL" \
	-L"$GCC_SYSLIBS" \
	-Wl,-rpath-link,"$HAIKU_RUNTIME_LIB" \
	-Wl,-rpath-link,"$GCC_SYSLIBS" \
	-shared-libgcc \
	-lbe -lroot -lstdc++ -lgcc_s \
	-o "$BUILD/uefi_wizard"

echo
echo "==> Done"
ls -lh "$BUILD/uefi_wizard"
