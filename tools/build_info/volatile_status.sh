#!/bin/bash

# SPDX-FileCopyrightText: Copyright 2026 Siradel
# SPDX-License-Identifier: MIT

COMMIT_HASH=$(git describe --always --abbrev=40 --long --dirty 2>/dev/null)

if [ -n "$COMMIT_HASH" ]; then
    echo "STABLE_GIT_COMMIT $COMMIT_HASH"
else
    echo "STABLE_GIT_COMMIT unknown"
fi
