#!/bin/bash
set -euo pipefail

# Minimal rebuild test for MPICH installer

ROOT_DIR=$(cd "$(dirname "$0")/../.." && pwd -P)
cd "$ROOT_DIR"

rm -rf build/mpich-4.3.1 install/mpich-4.3.1 || true

export TSANFLAGS=""
export CFLAGS="-O2 -fPIC -fno-omit-frame-pointer -fopenmp -g -mtune=native"
export CXXFLAGS="$CFLAGS"
export FCFLAGS="$CFLAGS -fbacktrace"
export LDFLAGS=""

./toolchain_gnu-mpich.sh

set +e
"$ROOT_DIR"/install/mpich-4.3.1/bin/mpiexec -n 1 /bin/true
status=$?
set -e

if [ "$status" -ne 0 ]; then
  echo "MPICH mpiexec basic run failed" >&2
  exit 1
fi

if ldd "$ROOT_DIR"/install/mpich-4.3.1/lib/libmpi.so | grep -q tsan; then
  echo "Unexpected libtsan linked in libmpi.so" >&2
  exit 1
fi

echo "MPICH rebuild test passed"