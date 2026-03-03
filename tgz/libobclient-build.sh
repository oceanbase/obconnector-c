#!/bin/bash
#
# Build script for creating libobclient tar.gz package on macOS.
# Modeled after rpm/libobclient-build.sh and rpm/libobclient.spec.
#
# Usage:
#   libobclient-build.sh                          # use defaults
#   libobclient-build.sh <topdir> <package> <version> <release>
#

set -e

#-----------------------------------------------------------------------
# 1. Platform detection
#-----------------------------------------------------------------------
OS_KERNEL="$(uname -s)"
OS_ARCH="$(uname -m)"

if [[ "$OS_KERNEL" != "Darwin" ]]; then
    echo "[ERROR] This script is intended for macOS (Darwin) only." 1>&2
    exit 1
fi

OS_TAG="darwin.${OS_ARCH}"

#-----------------------------------------------------------------------
# 2. Arguments / variables  (mirrors rpm/libobclient-build.sh)
#-----------------------------------------------------------------------
if [[ $# -ne 4 ]]; then
    TOP_DIR="$(cd "$(dirname "$0")/.."; pwd)"
    PACKAGE="$(basename "$0" -build.sh)"        # libobclient
    VERSION="$(cat "$(dirname "$0")/${PACKAGE}-VER.txt")"
    RELEASE="1"
else
    TOP_DIR="$1"
    PACKAGE="$2"
    VERSION="$3"
    RELEASE="$4"
fi

PREFIX="/u01/obclient"

echo "============================================================"
echo "[BUILD] Package  : ${PACKAGE}"
echo "[BUILD] Version  : ${VERSION}"
echo "[BUILD] Release  : ${RELEASE}"
echo "[BUILD] Platform : ${OS_TAG}"
echo "[BUILD] TOP_DIR  : ${TOP_DIR}"
echo "[BUILD] PREFIX   : ${PREFIX}"
echo "============================================================"

#-----------------------------------------------------------------------
# 3. Directories
#-----------------------------------------------------------------------
TMP_DIR="${TOP_DIR}/${PACKAGE}-tgz-tmp.$$"
INSTALL_ROOT="${TMP_DIR}/install"
OUTPUT_DIR="${TOP_DIR}/tgz"

mkdir -p "${INSTALL_ROOT}"
mkdir -p "${OUTPUT_DIR}"

#-----------------------------------------------------------------------
# 4. Build  (equivalent to %build in the spec)
#-----------------------------------------------------------------------
echo "[BUILD] Running build.sh ..."
cd "${TOP_DIR}"
bash build.sh

#-----------------------------------------------------------------------
# 5. Install into staging area  (equivalent to %install in the spec)
#-----------------------------------------------------------------------
echo "[BUILD] Installing into staging directory ..."
cd "${TOP_DIR}"
make DESTDIR="${INSTALL_ROOT}" install

# Remove .git metadata that may have been copied
find "${INSTALL_ROOT}" -name '.git' -type d -print0 | xargs -0 rm -rf 2>/dev/null || true

# Only keep bin / share / include / lib under PREFIX  (mirrors spec)
STAGING_PREFIX="${INSTALL_ROOT}${PREFIX}"
if [[ -d "${STAGING_PREFIX}" ]]; then
    for entry in "${STAGING_PREFIX}"/*; do
        base="$(basename "${entry}")"
        case "${base}" in
            bin|share|include|lib) ;;   # keep
            *) rm -rf "${entry}" ;;     # remove
        esac
    done
fi

#-----------------------------------------------------------------------
# 6. Package into tar.gz
#
#   Archive layout preserves the install prefix path:
#     u01/obclient/bin/mariadb_config
#     u01/obclient/include/...
#     u01/obclient/lib/...
#
#   Install: sudo tar -xzf <tarball> -C /
#-----------------------------------------------------------------------
TARBALL_NAME="${PACKAGE}-${VERSION}-${RELEASE}.${OS_TAG}.tar.gz"
echo "[BUILD] Creating tarball: ${TARBALL_NAME} ..."

echo "[BUILD] Tarball contents:"
(cd "${INSTALL_ROOT}" && find . -type f -o -type l | sort | sed 's|^\./||')

# Create the tar.gz from INSTALL_ROOT, preserving u01/obclient/... paths
# Tarball internal paths:  u01/obclient/bin/...  u01/obclient/include/...  etc.
cd "${INSTALL_ROOT}"
tar -czf "${OUTPUT_DIR}/${TARBALL_NAME}" u01

echo "[BUILD] Tarball created at: ${OUTPUT_DIR}/${TARBALL_NAME}"

#-----------------------------------------------------------------------
# 7. Cleanup
#-----------------------------------------------------------------------
rm -rf "${TMP_DIR}"

echo "============================================================"
echo "[BUILD] SUCCESS: ${TARBALL_NAME}"
echo "============================================================"
