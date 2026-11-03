#!/usr/bin/env bash
# Run one command in the isolated SEC-002 environment; never source this file.
# This selects a validated development baseline, not permanent-key approval.
set -eo pipefail

falafel_repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
falafel_sdk="$falafel_repo/.cache/sec002/esp-idf-v5.5.6"
falafel_tools="$falafel_repo/.cache/sec002/tools"
if [[ "$(git -C "$falafel_sdk" rev-parse HEAD)" != 880a203ae603d08a94d06aa01e0d6761958e5e9e ]] ||
   [[ "$(git -C "$falafel_sdk/components/mbedtls/mbedtls" rev-parse HEAD)" != 2b96dd8eebe880f304c69976b3c2fa0c5100cbb6 ]]; then
    echo 'SEC-002: SDK or Mbed TLS commit differs from the validated baseline.' >&2
    exit 1
fi
falafel_envs=("$falafel_tools"/python_env/idf5.5_py*_env)
if [[ ${#falafel_envs[@]} != 1 || ! -x "${falafel_envs[0]}/bin/python" ]]; then
    echo 'SEC-002: install the isolated Python environment; see firmware/README.md.' >&2
    exit 1
fi
export IDF_PATH="$falafel_sdk"
export IDF_TOOLS_PATH="$falafel_tools"
export IDF_PYTHON_ENV_PATH="${falafel_envs[0]}"
export PIP_CACHE_DIR="$falafel_repo/.cache/sec002/pip-cache"
export PATH="$IDF_PYTHON_ENV_PATH/bin:$PATH"
# The official script changes only this child process, not the caller's shell.
source "$IDF_PATH/export.sh" >/dev/null
if [[ $# == 0 ]]; then set -- idf.py --version; fi
exec "$@"
