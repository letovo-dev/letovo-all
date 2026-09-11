#!/usr/bin/env bash
set -euo pipefail

. src/backend-builder.lock

[[ "$BUILDER_IMAGE" =~ ^ghcr\.io/letovo-dev/letovo-backend-builder@sha256:[0-9a-f]{64}$ ]]
[[ "$BUILDER_LOCK_REVISION" =~ ^[0-9a-f]{64}$ ]]
[[ "$BUILDER_MANIFEST_REVISION" =~ ^[0-9a-f]{64}$ ]]

{
  echo "BUILDER_IMAGE=$BUILDER_IMAGE"
  echo "BUILDER_LOCK_REVISION=$BUILDER_LOCK_REVISION"
  echo "BUILDER_MANIFEST_REVISION=$BUILDER_MANIFEST_REVISION"
} >> "$GITHUB_ENV"

{
  echo "builder_image=$BUILDER_IMAGE"
  echo "builder_lock_revision=$BUILDER_LOCK_REVISION"
  echo "dependency_manifest_revision=$BUILDER_MANIFEST_REVISION"
} >> "$GITHUB_STEP_SUMMARY"
