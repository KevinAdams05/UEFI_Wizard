#!/usr/bin/env bash
#
# build-hpkg.sh — cross-build UEFI Wizard and produce a standalone .hpkg
#
# Runs on the Haiku Linux cross-build server (kevin@192.168.74.122).
# Expects the project tree (src/, package/) to be present at the script's
# parent directory.
#
# Output: build/uefi_wizard-<version>-<arch>.hpkg
#
# Override paths via env vars if needed:
#   HAIKU_BUILD   — top of the haiku source checkout (default: ~/haiku-build/haiku)
#   HAIKU_ARCH    — target arch (default: x86_64)

set -euo pipefail

HAIKU_BUILD="${HAIKU_BUILD:-$HOME/haiku-build/haiku}"
HAIKU_ARCH="${HAIKU_ARCH:-x86_64}"

GENERATED="$HAIKU_BUILD/generated.${HAIKU_ARCH}"
CROSS_BIN="$GENERATED/cross-tools-${HAIKU_ARCH}/bin"
HOST_TOOLS="$GENERATED/objects/linux/${HAIKU_ARCH}/release/tools"

CXX="$CROSS_BIN/${HAIKU_ARCH}-unknown-haiku-g++"
RC="$HOST_TOOLS/rc/rc"
XRES="$HOST_TOOLS/xres"
MIMESET="$HOST_TOOLS/mimeset/mimeset"
PACKAGE="$HOST_TOOLS/package/package"

HAIKU_KITS="$GENERATED/objects/haiku/${HAIKU_ARCH}/release/kits"
HAIKU_HEADERS="$HAIKU_BUILD/headers"

# haiku_devel.hpkg has CRT files (crti.o, crtn.o, start_dyn.o) and static
# .a archives. The .so symlinks in develop/lib only resolve once
# haiku.hpkg is also installed, so at build time we pull the real .so files
# from the sibling haiku.hpkg contents tree.
HAIKU_DEVEL_LIB="$GENERATED/objects/haiku/${HAIKU_ARCH}/packaging/packages_build/regular/hpkg_-haiku_devel.hpkg/contents/develop/lib"
HAIKU_RUNTIME_LIB="$GENERATED/objects/haiku/${HAIKU_ARCH}/packaging/packages_build/regular/hpkg_-haiku.hpkg/contents/lib"

GCC_SYSLIBS_DEVEL=$(ls -d "$GENERATED/build_packages"/gcc_syslibs_devel-*-"${HAIKU_ARCH}"/develop/lib 2>/dev/null | head -1)
GCC_SYSLIBS=$(ls -d "$GENERATED/build_packages"/gcc_syslibs-*-"${HAIKU_ARCH}"/lib 2>/dev/null | head -1)
if [ -z "$GCC_SYSLIBS_DEVEL" ] || [ -z "$GCC_SYSLIBS" ]; then
	echo "ERROR: gcc_syslibs[_devel] not found under $GENERATED/build_packages/" >&2
	exit 1
fi

PROJ="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$PROJ/src"
BUILD="$PROJ/build"
PKG_ROOT="$BUILD/package_root"
PKG_INFO_TEMPLATE="$PROJ/package/PackageInfo"

# ------------------------------------------------------------------
# Sanity checks
# ------------------------------------------------------------------
for tool in "$CXX" "$RC" "$PACKAGE"; do
	if [ ! -x "$tool" ]; then
		echo "ERROR: required tool not found or not executable: $tool" >&2
		echo "       Build the Haiku tree first:" >&2
		echo "         cd $GENERATED && jam -q -j4 \\<build\\>haiku_devel.hpkg" >&2
		exit 1
	fi
done

if [ ! -f "$HAIKU_KITS/libbe.so" ]; then
	echo "ERROR: libbe.so not found at $HAIKU_KITS/" >&2
	echo "       Run a full Haiku build first: jam -q -j4 @nightly-anyboot" >&2
	exit 1
fi

# ------------------------------------------------------------------
# Clean staging
# ------------------------------------------------------------------
rm -rf "$BUILD"
mkdir -p "$BUILD" "$PKG_ROOT/apps" "$PKG_ROOT/bin" \
	"$PKG_ROOT/data/deskbar/menu/Applications" \
	"$PKG_ROOT/data/documentation/packages/uefi_wizard"

# ------------------------------------------------------------------
# Compile shared sources (used by both GUI and CLI)
# ------------------------------------------------------------------
SHARED_SOURCES=(
	ArchUtils.cpp
	Constants.cpp
	ESPManager.cpp
	LoaderInstaller.cpp
)

GUI_SOURCES=(
	App.cpp
	DiskPicker.cpp
	MainWindow.cpp
	WorkerThread.cpp
)

CLI_SOURCES=(
	cli/UEFIWizardCLI.cpp
)

INCLUDES=(
	-I"$HAIKU_HEADERS"
	-I"$HAIKU_HEADERS/os"
	-I"$HAIKU_HEADERS/os/app"
	-I"$HAIKU_HEADERS/os/interface"
	-I"$HAIKU_HEADERS/os/locale"
	-I"$HAIKU_HEADERS/os/storage"
	-I"$HAIKU_HEADERS/os/support"
	-I"$HAIKU_HEADERS/os/kernel"
	-I"$HAIKU_HEADERS/os/drivers"
	-I"$HAIKU_HEADERS/posix"
	-I"$HAIKU_HEADERS/config"
	-I"$HAIKU_HEADERS/private/interface"
	-I"$HAIKU_HEADERS/private/shared"
	-I"$HAIKU_HEADERS/private/storage"
	-I"$SRC"
)

CXXFLAGS=(
	-O2 -Wall -Wno-multichar
	-fno-strict-aliasing
	"${INCLUDES[@]}"
)

compile_one() {
	local s="$1"
	local obj="$BUILD/$(basename "${s%.cpp}").o"
	echo "    $s" >&2
	"$CXX" "${CXXFLAGS[@]}" -c "$SRC/$s" -o "$obj"
	printf "%s" "$obj"
}

echo "==> Compiling shared sources..."
SHARED_OBJS=()
for s in "${SHARED_SOURCES[@]}"; do
	SHARED_OBJS+=("$(compile_one "$s")")
done

echo "==> Compiling GUI sources..."
GUI_OBJS=()
for s in "${GUI_SOURCES[@]}"; do
	GUI_OBJS+=("$(compile_one "$s")")
done

echo "==> Compiling CLI sources..."
CLI_OBJS=()
for s in "${CLI_SOURCES[@]}"; do
	CLI_OBJS+=("$(compile_one "$s")")
done

# ------------------------------------------------------------------
# Link
# ------------------------------------------------------------------
LINK_FLAGS=(
	-B"$HAIKU_DEVEL_LIB"
	-L"$HAIKU_DEVEL_LIB"
	-L"$HAIKU_RUNTIME_LIB"
	-L"$GCC_SYSLIBS_DEVEL"
	-L"$GCC_SYSLIBS"
	-Wl,-rpath-link,"$HAIKU_RUNTIME_LIB"
	-Wl,-rpath-link,"$GCC_SYSLIBS"
	-shared-libgcc
)

echo "==> Linking GUI binary UEFIWizard..."
"$CXX" "${SHARED_OBJS[@]}" "${GUI_OBJS[@]}" \
	"${LINK_FLAGS[@]}" \
	-lbe -lroot -lstdc++ -lgcc_s \
	"$HAIKU_DEVEL_LIB/libcolumnlistview.a" \
	"$HAIKU_DEVEL_LIB/liblocalestub.a" \
	-o "$BUILD/UEFIWizard"

echo "==> Linking CLI binary uefi_wizard..."
"$CXX" "${SHARED_OBJS[@]}" "${CLI_OBJS[@]}" \
	"${LINK_FLAGS[@]}" \
	-lbe -lroot -lstdc++ -lgcc_s \
	-o "$BUILD/uefi_wizard"

# ------------------------------------------------------------------
# Resources (GUI binary only)
# ------------------------------------------------------------------
echo "==> Compiling resources..."
"$RC" -o "$BUILD/UEFIWizard.rsrc" "$SRC/UEFIWizard.rdef"

echo "==> Attaching resources..."
"$XRES" -o "$BUILD/UEFIWizard" "$BUILD/UEFIWizard.rsrc"

if [ -x "$MIMESET" ]; then
	echo "==> Setting MIME info..."
	"$MIMESET" -F "$BUILD/UEFIWizard" || true
fi

# ------------------------------------------------------------------
# Stage the package tree
# ------------------------------------------------------------------
echo "==> Staging package tree..."
cp "$BUILD/UEFIWizard" "$PKG_ROOT/apps/UEFIWizard"
chmod +x "$PKG_ROOT/apps/UEFIWizard"

# Mirror the resources as file *attributes* on the staged binary.
# BAboutWindow, Tracker, and Deskbar resolve the app icon through the
# attribute copy (BAppFileInfo), not the resource section — a native
# on-Haiku build gets the attributes from mimeset, but on the Linux
# host we have to write them explicitly. resattr uses the same host
# attribute-emulation layer as the package tool, so `package create`
# picks them up into the .hpkg.
RESATTR="$HOST_TOOLS/resattr/resattr"
echo "==> Mirroring resources to attributes..."
"$RESATTR" -O -o "$PKG_ROOT/apps/UEFIWizard" "$BUILD/UEFIWizard.rsrc"

cp "$BUILD/uefi_wizard" "$PKG_ROOT/bin/uefi_wizard"
chmod +x "$PKG_ROOT/bin/uefi_wizard"

ln -sf ../../../../apps/UEFIWizard \
	"$PKG_ROOT/data/deskbar/menu/Applications/UEFI Wizard"

cp "$PROJ/LICENSE" \
	"$PKG_ROOT/data/documentation/packages/uefi_wizard/LICENSE"
cp "$PROJ/README.md" \
	"$PKG_ROOT/data/documentation/packages/uefi_wizard/README.md"

cp "$PKG_INFO_TEMPLATE" "$PKG_ROOT/.PackageInfo"

# ------------------------------------------------------------------
# Build the .hpkg
# ------------------------------------------------------------------
VERSION=$(awk '/^version/ { gsub(/[ \t]+/, " "); print $2 }' "$PKG_INFO_TEMPLATE")
HPKG="$BUILD/uefi_wizard-${VERSION}-${HAIKU_ARCH}.hpkg"

echo "==> Creating $HPKG..."
rm -f "$HPKG"
( cd "$PKG_ROOT" && "$PACKAGE" create -q "$HPKG" )

echo
echo "==> Done"
echo "    $HPKG"
ls -lh "$HPKG"
