#!/usr/bin/env bash
set -euo pipefail
# Fixed paths exist only inside the disposable VM; no inherited credentials.
cd /tmp/letovo-ci
mkdir -p home docker-config
worker_uid="$(id -u)"
exec env -i PATH=/usr/local/bin:/usr/bin:/bin HOME=/tmp/letovo-ci/home \
  DOCKER_HOST="unix:///run/user/$worker_uid/docker.sock" \
  DOCKER_CONFIG=/tmp/letovo-ci/docker-config LANG=C.UTF-8 \
  timeout --kill-after=30 1800 /bin/bash -euo pipefail -c '
    python3 /tmp/letovo-ci/control/builder_artifact.py extract-source \
      /tmp/letovo-ci/source.tar /tmp/letovo-ci/source
    /bin/bash /tmp/letovo-ci/control/build-builder.sh \
      /tmp/letovo-ci/source /tmp/letovo-ci/source/request.json /tmp/letovo-ci/output >&2
    python3 /tmp/letovo-ci/control/builder_artifact.py pack-result \
      /tmp/letovo-ci/output /tmp/letovo-ci/result.tar
  '
