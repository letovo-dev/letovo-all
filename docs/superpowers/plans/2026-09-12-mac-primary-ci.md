# Mac mini Primary CI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the Mac mini the primary compute host for PR, main, and release verification/builds while GitHub remains the trusted publisher/deployer and retains a bounded hosted fallback.

**Architecture:** A trusted GitHub controller packages a frozen allowlisted source tree and calls a forced-command Mac supervisor through a loopback-only reverse SSH tunnel. The supervisor runs the shared build contract in a disposable warm Lima clone and returns verified image archives; GitHub validates and publishes those archives without rebuilding. PR requests are unprivileged and the trusted controller reports required status contexts on the frozen head SHA.

**Tech Stack:** Bash, Python 3 standard library, Docker Buildx, Lima 2.2, OpenSSH, GitHub Actions, pytest.

**Spec:** `docs/superpowers/specs/2026-09-12-mac-primary-ci-design.md`

## Global Constraints

- Target platform is exactly `linux/amd64`.
- Builder is resolved by `scripts/export_backend_builder.sh` and must be an immutable `ghcr.io/letovo-dev/letovo-backend-builder@sha256:<64 hex>` reference.
- Mac-first is disabled only when repository variable `MAC_CI_ENABLED` is exactly `false`.
- Pre-start unavailable/busy may fall back once; any failure after `LETOVO_REMOTE_STARTED` must fail closed.
- Remote execution timeout is 1800 seconds; controller timeout is 60 minutes; SSH readiness timeout is at most 15 seconds.
- Mac receives no GHCR write token, GitHub token, submodule key, deploy key, or production secret.
- Source archives and extraction contain regular files/directories only and never `.git`, `certs`, absolute paths, `..`, symlinks, devices, FIFOs, or sockets. Packing validates and omits only the exact four tracked `src/configs` runtime links to `/mnt/server-configs`; the build recreates them in the per-run source tree.
- Expected images are exactly backend, registration, frontend, and uploader; profile is exactly `candidate` or `production`.
- Publisher verifies repository, run ID, run attempt, source SHA, frontend gitlink, profile, platform, archive checksum, local image ID, and complete image set before push.
- Publisher scripts contain no `docker build`/`docker buildx build`; build scripts contain no GHCR login or push.
- Existing `backend-pr`, `frontend-verify`, and `live-deployment-e2e` required contexts must fail rather than skip when prerequisites fail.
- Preview production migration remains build-free; apply builds before the protected deploy/migration job and preserves all existing safety gates.
- Do not initialize or transfer the `certs` submodule.
- Keep the existing unrelated dirty root checkout untouched.

---

### Task 1: Source and image bundle contract

**Files:**
- Create: `scripts/ci/source_archive.py`
- Create: `scripts/ci/image_manifest.py`
- Create: `scripts/ci/build-bundle.sh`
- Create: `scripts/ci/publish-bundle.sh`
- Create: `test/test_issue214_ci_bundle.py`

**Interfaces:**
- Produces: `source_archive.py pack ROOT REQUEST_JSON OUTPUT` and `extract ARCHIVE DEST`; `image_manifest.py create REQUEST OUTPUT_DIR` and `verify MANIFEST EXPECTED_JSON BUNDLE_DIR`; `build-bundle.sh SOURCE_ROOT REQUEST_JSON OUTPUT_DIR`; `publish-bundle.sh BUNDLE_DIR EXPECTED_JSON TAG_MODE`.
- Consumes: `scripts/export_backend_builder.sh`, current Dockerfiles, frontend npm scripts, sanitizer source, PostgreSQL regression tests.

- [ ] Write failing pytest cases proving archive allowlisting/path rejection, complete manifest validation, checksum/SHA/profile/platform/run/image-set rejection, and the no-build-in-publisher/no-push-in-builder invariant.
- [ ] Run `python3 -m pytest -q -p no:cacheprovider test/test_issue214_ci_bundle.py`; verify failures are caused by missing scripts.
- [ ] Implement `source_archive.py` with `argparse`, `pathlib`, and `tarfile`; include only `src`, `frontend`, `test`, `scripts/export_backend_builder.sh`, and the migration files named in the spec. Validate and omit only the exact four tracked runtime config symlinks; reject any other symlink or target mismatch and every non-regular archive/extraction entry.
- [ ] Implement `image_manifest.py` with `json`, `hashlib`, and `subprocess`; require schema version 1, exact metadata fields, four unique image records, lowercase SHA-256 values, `linux/amd64`, and `docker image inspect` ID/architecture equality after load.
- [ ] Implement `build-bundle.sh`: validate metadata; recreate the four runtime config symlinks in the per-run source tree; run sanitizer in the pinned builder, PostgreSQL 16 regressions in a unique container/database, existing frontend checks/build/route scan, build each profile image once with `--platform linux/amd64 --load`, inspect it, `docker save | zstd -1`, create reports/manifest, and never push.
- [ ] Implement `publish-bundle.sh`: run static validation, verify archive checksums, stream each archive through `zstd -dc | docker load`, verify image ID/architecture, tag only the expected candidate/SHA or main/release tags, push, inspect registry digest, and append `registry-digests.json`; never build.
- [ ] Run the focused pytest file and `bash -n scripts/ci/build-bundle.sh scripts/ci/publish-bundle.sh`; expect pass.
- [ ] Commit with message `ci: add verified build artifact contract`.

### Task 2: Bounded Mac transport and disposable supervisor

**Files:**
- Create: `scripts/ci/try-mac.sh`
- Create: `scripts/ci/mac-supervisor.sh`
- Create: `scripts/ci/run-build.sh`
- Create: `test/test_issue214_mac_transport.py`
- Create: `docs/ci-mac-mini.md`

**Interfaces:**
- Consumes: source archive and request JSON from Task 1; installed golden instance name `letovo-ci-template`; worker entrypoint `scripts/ci/run-build.sh`.
- Produces: exit 0 plus result archive; exit 75 plus a fallback reason only before remote start; any other nonzero after `LETOVO_REMOTE_STARTED`; summary key/value file with executor and phase timings.

- [ ] Write failing subprocess tests with fake `ssh`/`limactl` executables for disabled, offline, busy, started build failure, post-start transport loss, timeout, stale request, concurrent lock, cleanup, and result-archive absence.
- [ ] Run `python3 -m pytest -q -p no:cacheprovider test/test_issue214_mac_transport.py`; verify expected missing-interface failures.
- [ ] Implement `try-mac.sh` with injectable `SSH_BIN`, 15-second connection bounds, strict host-key config, one SSH invocation, marker-aware exit classification, no retry, and atomic result placement.
- [ ] Implement `mac-supervisor.sh` as the forced command: accept only `run`, acquire native macOS non-blocking `lockf`, validate/extract the archive and its fresh transport identity header, emit the start marker, create a run-scoped Lima clone, copy the source/control files, run guest `timeout --kill-after=30 1800` with a Python stdlib host deadline, return the result archive on stdout, and trap cleanup of only its process group/VM/state directory.
- [ ] Implement `run-build.sh` as the VM shim invoking the trusted Task 1 build script with fixed paths and no secret environment forwarding.
- [ ] Document exact golden-template packages/images, reverse tunnel, restricted authorized-key options, launchd unit, GitHub secrets/variables, rotation, rollback, manual probes, and cleanup in `docs/ci-mac-mini.md`; use `ya.sergeiscv.ru` loopback port 22222.
- [ ] Run focused tests and `bash -n` for all three scripts; expect pass.
- [ ] Commit with message `ci: add bounded Mac build transport`.

### Task 3: Mac-first PR and main workflows

**Files:**
- Create: `.github/workflows/pr-ci-request.yml`
- Create: `.github/workflows/mac-ci-pilot.yml`
- Create: `scripts/ci/report-status.sh`
- Modify: `.github/workflows/docker-image.yml`
- Modify: `test/test_live_e2e_workflow_contract.py`
- Modify: `test/test_issue174_uploader_deployment_contract.py`
- Create: `test/test_issue214_mac_workflow_contract.py`

**Interfaces:**
- Consumes: Tasks 1-2 CLIs, repo secrets `MAC_CI_SSH_KEY` and `MAC_CI_KNOWN_HOSTS`, vars `MAC_CI_ENABLED`, `MAC_CI_BASTION_HOST`, `MAC_CI_BASTION_USER`, `MAC_CI_WORKER_USER`, and `MAC_CI_WORKER_PORT`.
- Produces: artifact name `letovo-images-<run_id>-<run_attempt>-<profile>-<source_sha>`; statuses `backend-pr`, `frontend-verify`, `live-deployment-e2e`; unchanged candidate and main tags.

- [ ] Replace old positive string assertions with failing contract tests for unprivileged PR request, trusted `workflow_run`, frozen head/merge SHA, explicit public frontend checkout, no certs/submodule secret, Mac-first/fallback classification, `if: always()` failure propagation, exact artifact identity, publisher-only push, required statuses, unchanged candidate deployment/restore, and `latest` only on trusted main.
- [ ] Run the three focused contract files; verify they fail against the hosted-build workflow.
- [ ] Implement `report-status.sh` with the GitHub commit-status API, exact allowlisted contexts/states, and no-op behavior outside PR controller mode.
- [ ] Add `pr-ci-request.yml` with `contents: read`, no secrets, no write permission, and a single completion job.
- [ ] Rewrite build portions of `docker-image.yml` to resolve frozen context, checkout trusted control and candidate source separately, fetch public frontend at the validated gitlink, package source, try Mac first, run the same build script once on hosted fallback, upload the bundle, make required wrapper checks fail explicitly, and publish only via `publish-bundle.sh`. Preserve the full existing live deployment, migrations, browser smoke, and restore body.
- [ ] Add `mac-ci-pilot.yml` as `workflow_dispatch` build-only validation with normal/disabled/offline/busy/remote-failure modes and no package/deploy permission.
- [ ] Run focused tests plus all `test/test_*contract.py`; record any pre-existing unrelated failure separately.
- [ ] Commit with message `ci: make Mac primary for PR and main builds`.

### Task 4: Production release artifact path

**Files:**
- Modify: `.github/workflows/production-release.yml`
- Modify: `test/test_live_e2e_workflow_contract.py`
- Modify: `test/test_issue174_uploader_deployment_contract.py`
- Modify: `test/test_issue214_mac_workflow_contract.py`

**Interfaces:**
- Consumes: Tasks 1-2 build bundle, frozen main SHA output, production base URL, current production environment/deploy inputs.
- Produces: verified production image tags at the frozen SHA before migrations/deploy; no image rebuild in protected release job.

- [ ] Add failing assertions that preview contains no build, apply freezes main SHA in a pre-deploy build job, Mac-first/fallback uses production profile, release downloads the exact run artifact and calls `publish-bundle.sh`, all Docker build actions/commands are absent from the deploy job, and existing migration/backup/smoke/rollback/E2E assertions remain.
- [ ] Run focused production contract tests; verify failures identify the old inline build steps.
- [ ] Add `build-release-images` before the protected `release` job, output frozen SHA/artifact identity, and use Task 3's Mac-first/hosted fallback pattern without production/package secrets.
- [ ] Make `release` depend on the build job, checkout the frozen SHA, download/verify/publish its production bundle before existing deployment mutations, then preserve the existing transfer, migration, deployment, rollback, and E2E sequence verbatim.
- [ ] Run focused tests and all contract tests; expect no new failures.
- [ ] Commit with message `ci: consume Mac build artifacts in releases`.

### Task 5: End-to-end integration and rollout evidence

**Files:**
- Modify only as findings require: Task 1-4 files and `docs/ci-mac-mini.md`
- Test: all issue #214 tests and workflow contract tests

**Interfaces:**
- Consumes: merged Phase 1 workflow/scripts, configured reverse tunnel, golden VM, repository secrets/vars.
- Produces: Phase 2 enabling Mac-first by default, real PR/main Mac evidence, real hosted fallback evidence, and issue #214 completion report.

- [ ] Run `python3 -m pytest -q -p no:cacheprovider test/test_issue214_ci_bundle.py test/test_issue214_mac_transport.py test/test_issue214_mac_workflow_contract.py test/test_live_e2e_workflow_contract.py test/test_issue174_uploader_deployment_contract.py test/test_issue215_backend_builder_contract.py` and `bash -n scripts/ci/*.sh`.
- [ ] Measure one local hosted bundle and one Mac supervisor bundle; validate/load without push, compare manifests, image architecture, archive sizes, and reports.
- [ ] After explicit security approval, install restricted tunnel/controller keys, supervisor, launchd tunnel, and stopped `letovo-ci-template`; configure GitHub secrets/vars and read back names/configuration without exposing values.
- [ ] Open the Phase 1 PR, request `ya-yara`, address review, and wait for green existing CI before merge.
- [ ] Open a minimal Phase 2 PR after Phase 1 is on main; verify real Mac candidate build, publisher, required statuses, candidate live E2E/restore, and `ya-yara` approval before merge.
- [ ] Verify the Phase 2 main push builds on Mac, publishes exact SHA/`latest`, and downstream main live E2E succeeds.
- [ ] Run manual pilot unavailable and busy modes; verify one hosted fallback. Run injected post-start failure; verify red with no fallback. Verify wrong checksum/SHA/platform/image-set tests remain red.
- [ ] Read back final repository workflow/variable/secret names, bastion loopback listener, Mac launchd state, no GHCR credential inside disposable worker, and no leftover run VM/state.
- [ ] Post timings, artifact sizes, fault evidence, PRs, and rollback command to issue #214 and close it only after all evidence is green.
