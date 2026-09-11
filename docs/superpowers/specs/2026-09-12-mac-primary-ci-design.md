# Mac mini Primary CI Design

Issue: https://github.com/letovo-dev/letovo-all/issues/214

## Goal

Use the Mac mini as the primary compute host for verification and all four
application image builds. GitHub-hosted runners remain the trusted controller,
artifact boundary, GHCR publisher, and deployment boundary. A Mac that is
disabled, offline, or busy falls back once to the same hosted build path.

## Trust boundary

- Pull requests run a tiny unprivileged `pull_request` request workflow.
- A `workflow_run` workflow loaded from the default branch resolves and freezes
  the PR head and merge SHAs. It ignores artifacts and scripts supplied by the
  request workflow.
- The controller checks out the frozen source without recursive submodules and
  fetches the public frontend repository from the trusted HTTPS URL at the
  exact gitlink. `certs` is never initialized or transferred.
- Only allowlisted regular files below `src`, `frontend`, `test`, selected
  migration files, and trusted CI inputs enter the source archive. Symlinks,
  special files, absolute paths, `..`, `.git`, and secret material are rejected.
- Untrusted PR source executes only in a disposable Lima clone. The clone gets
  no GitHub, GHCR, deployment, SSH, or production secret. It is deleted after
  every run.
- Mac output is untrusted input to the publisher. A manifest, archive SHA-256,
  local image ID, platform, profile, source SHA, frontend gitlink, run ID, and
  run attempt are verified before `docker load`, tag, and push.
- The Mac never logs into GHCR in the production design. The dedicated token
  used for the benchmark is not part of CI.

## Execution and fallback

- `MAC_CI_ENABLED=false` explicitly selects hosted execution. Any other value
  selects Mac first.
- A single forced-command SSH session transfers the source archive, acquires a
  non-blocking `flock`, clones the golden Lima template, runs the trusted worker,
  and returns the result archive.
- Connect/readiness is bounded to 15 seconds. Remote execution is bounded to 30
  minutes and the controller job to 60 minutes.
- Disabled, pre-start connection failure, readiness failure, or busy lock exits
  with the documented fallback code and starts one hosted build.
- Once the supervisor emits the started marker, a build/test failure, timeout,
  missing report, or transport loss fails the check. It never falls back to hide
  a real failure.
- Cancellation terminates the worker process group and removes only that run's
  disposable VM and state directory.

## Network

The Mac maintains an outbound reverse SSH tunnel to `ya.sergeiscv.ru`, the
non-production candidate host. The forwarding socket listens only on bastion
loopback. Separate tunnel and controller keys are restricted with
`permitlisten` and `permitopen`; the controller key is forced to the installed
Mac supervisor. No agent forwarding, PTY, shell, or public Docker API is
enabled. GitHub pins both bastion and Mac SSH host keys.

## Build contract

The trusted `build-bundle.sh` is shared by Mac and hosted fallback. It:

1. validates request metadata and the pinned backend builder digest;
2. runs the existing sanitizer and PostgreSQL regressions;
3. runs frontend `npm ci`, the four existing frontend checks, production-style
   build, and forbidden-route scan;
4. builds backend, registration, frontend, and uploader once for the selected
   `candidate` or `production` profile as `linux/amd64`;
5. inspects image IDs and architecture;
6. exports one compressed archive per image plus reports and a manifest.

Candidate and production URLs remain distinct. The candidate uploader uses the
candidate capabilities URL. `latest` is assigned only by the trusted main
publisher. A release uses a frozen main SHA and production profile.

## Workflows

- `pr-ci-request.yml`: unprivileged PR completion signal only.
- `docker-image.yml`: trusted PR controller via `workflow_run`, direct main
  controller via `push`, Mac-first build with hosted fallback, status contexts,
  publisher-only GHCR jobs, and the existing candidate live E2E/restore path.
- `mac-ci-pilot.yml`: manual build-only exercise for normal, disabled/offline,
  busy, and injected remote-failure paths; it never publishes or deploys.
- `production-release.yml`: preview remains build-free. Apply freezes main,
  builds a production bundle before the protected deploy job, and the deploy job
  verifies/publishes the bundle without rebuilding before existing migration,
  backup, smoke, deploy, rollback, and post-deploy E2E steps.

Required contexts `backend-pr`, `frontend-verify`, and
`live-deployment-e2e` are posted against the frozen PR head SHA by the trusted
controller. Missing or skipped work is an error, not success.

## Acceptance criteria

- A same-repository PR and a main push use the Mac when it is ready.
- All four images are built once, exported, verified, and published as
  `linux/amd64`; publisher and release jobs contain no Docker build command.
- Existing regression, frontend, candidate deployment/restore, production
  migration/backup/rollback, and live E2E contracts remain enforced.
- Mac offline, disabled, or busy causes one hosted fallback; a started remote
  failure stays red.
- Wrong SHA, gitlink, profile, platform, checksum, image ID, missing image, or
  stale run metadata blocks publication.
- The Mac receives no GHCR write token or production/deployment secret.
- A real normal Mac run, real hosted fallback, and executable fault-contract
  tests are recorded in the workflow summary and issue.

