#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 || $# -gt 2 ]]; then
    cat <<'USAGE'
Usage: setup_trdp.sh <install-prefix> [<make-config-target>]

Downloads the latest TRDP stack release from SourceForge, builds it using the
upstream Makefiles, and stages the headers and libraries inside
<install-prefix>/include/trdp and <install-prefix>/lib.

Arguments:
  install-prefix       Writable directory that will receive the staged headers
                       and libraries.
  make-config-target   Optional Makefile target that selects the platform
                      configuration (defaults to LINUX_X86_64_config).

After the script finishes, point CMake at the prefix via:

  cmake -S backend -B build/backend -DTRDP_ROOT=<install-prefix>

or pass the same cache entries from the repository root so the backend build
can discover the freshly staged TRDP libraries.
USAGE
    exit 1
fi

INSTALL_PREFIX="$1"
MAKE_CONFIG_TARGET="${2:-LINUX_X86_64_config}"
DOWNLOAD_URL="${TRDP_DOWNLOAD_URL:-https://sourceforge.net/projects/tcnopen/files/latest/download}"
ARCHIVE_NAME="trdp-stack.zip"
WORK_DIR="$(mktemp -d)"
trap 'rm -rf "${WORK_DIR}"' EXIT

mkdir -p "${INSTALL_PREFIX}/include/trdp" "${INSTALL_PREFIX}/lib"

if [[ -f "${INSTALL_PREFIX}/lib/libtrdp.a" ]]; then
    echo "[setup-trdp] Existing libtrdp.a detected under ${INSTALL_PREFIX}; skipping rebuild."
    exit 0
fi

command -v curl >/dev/null 2>&1 || { echo "curl is required" >&2; exit 1; }
command -v unzip >/dev/null 2>&1 || { echo "unzip is required" >&2; exit 1; }
command -v make >/dev/null 2>&1 || { echo "make is required" >&2; exit 1; }

pushd "${WORK_DIR}" >/dev/null

echo "[setup-trdp] Downloading TRDP stack archive..."
curl -L "${DOWNLOAD_URL}" -o "${ARCHIVE_NAME}"

echo "[setup-trdp] Extracting archive..."
unzip -q "${ARCHIVE_NAME}"

SRC_ROOT="$(find . -maxdepth 1 -mindepth 1 -type d | head -n 1)"
if [[ -z "${SRC_ROOT}" ]]; then
    echo "Failed to detect extracted TRDP directory" >&2
    exit 1
fi

pushd "${SRC_ROOT}" >/dev/null

echo "[setup-trdp] Selecting build configuration: ${MAKE_CONFIG_TARGET}"
make "${MAKE_CONFIG_TARGET}"

echo "[setup-trdp] Building TRDP libraries..."
make -j"$(nproc)" FULL_BUILD=1 libtrdp libtrdpap

OUTPUT_LIB="$(find bld/output -type f -name 'libtrdp.a' | head -n 1 || true)"
if [[ -z "${OUTPUT_LIB}" ]]; then
    echo "Failed to locate libtrdp*.a in bld/output" >&2
    exit 1
fi

cp -R src/api/. "${INSTALL_PREFIX}/include/trdp/"
if [[ -d src/vos/api ]]; then
    cp -R src/vos/api/. "${INSTALL_PREFIX}/include/trdp/"
fi
cp "${OUTPUT_LIB}" "${INSTALL_PREFIX}/lib/"

EXTRA_AP_LIB="$(find bld/output -type f -name 'libtrdpap*.a' | head -n 1 || true)"
if [[ -n "${EXTRA_AP_LIB}" ]]; then
    cp "${EXTRA_AP_LIB}" "${INSTALL_PREFIX}/lib/"
fi

echo "[setup-trdp] TRDP installed under ${INSTALL_PREFIX}"

popd >/dev/null
popd >/dev/null
