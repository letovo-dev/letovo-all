#!/usr/bin/env bash
set -euo pipefail
[ "${PR_CONTROLLER:-}" = true ] || exit 0
[ "$#" -eq 3 ] || exit 2
[[ "$1" =~ ^[0-9a-f]{40}$ ]] || exit 2
case "$2" in backend-pr|frontend-verify|live-deployment-e2e) ;; *) exit 2 ;; esac
case "$3" in pending|success|failure|error) ;; *) exit 2 ;; esac
[ "${GITHUB_REPOSITORY:-}" = letovo-dev/letovo-all ] || exit 2
[[ "${GITHUB_RUN_ID:-}" =~ ^[1-9][0-9]*$ ]] || exit 2
gh api --method POST "repos/letovo-dev/letovo-all/statuses/$1" \
  -f "context=$2" -f "state=$3" \
  -f "target_url=https://github.com/letovo-dev/letovo-all/actions/runs/$GITHUB_RUN_ID" \
  -f "description=Mac CI: $3" >/dev/null
