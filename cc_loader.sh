#!/bin/sh

# Exit immediately on error
set -e

#S_FLAG is set if we want to switch to the newly loaded congestion control module after loading it
S_FLAG=0

while getopts "s" opt; do
    case "${opt}" in
        s)
            S_FLAG=1
            ;;
        *)
            echo "Usage: $0 [-s] <module_name>"
            exit 1
            ;;
    esac
done

shift $((OPTIND - 1))

CC_NAME="$1"
KO_FILE="${CC_NAME}.ko"
MODDIR="/usr/src/sys/modules/cc/${CC_NAME}"

if [ ! -d "${MODDIR}" ]; then
    echo "ERROR: module directory ${MODDIR} not found"
    exit 1
fi

echo "==> Congestion control module: ${CC_NAME}"

# Step 1: Clean and build
echo "==> Running make clean"
cd "${MODDIR}"
make clean

echo "==> Building module"
make

OBJDIR=$(make -V .OBJDIR)
KO_PATH="${OBJDIR}/${CC_NAME}.ko"

# Step 3: Unload if already loaded (non-fatal)
if kldstat -n "${CC_NAME}" >/dev/null 2>&1; then
    echo "==> Module already loaded, unloading first"
    kldunload -f "${CC_NAME}" || true
fi

# Step 4: Load module
echo "==> Loading module"
kldload "${KO_PATH}"

# Step 5: Verify load
echo "==> Verifying module is loaded"
kldstat -n "${CC_NAME}"

# Step 6: Check congestion control availability
echo "==> Available TCP congestion control algorithms:"
sysctl net.inet.tcp.cc.available

if [ "${S_FLAG}" -eq 1 ]; then
    echo "==> Switching to ${CC_NAME}"
    sysctl net.inet.tcp.cc.algorithm=${CC_NAME#cc_}
fi
echo "==> Done"
