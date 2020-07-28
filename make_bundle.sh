#!/bin/bash

cd "$(dirname "$BASH_SOURCE")/build" || { echo "Cannot bundle: not built yet" >&2; exit 1; }
cpack
