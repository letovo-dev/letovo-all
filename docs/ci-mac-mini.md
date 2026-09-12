# Mac mini CI operations

This is the installation contract, not evidence that the host is configured.
The GitHub controller calls `scripts/ci/try-mac.sh SOURCE_ARCHIVE REQUEST_JSON
RESULT_ARCHIVE SUMMARY_FILE` once. Exit 75 permits one hosted build; exit 0
returns a complete uncompressed tar; every other status is terminal. After the
exact stderr line `LETOVO_REMOTE_STARTED`, all failures are terminal. Only
result bytes use supervisor stdout. The controller writes executor, reason,
start state, readiness seconds and transport seconds to the summary file;
supervisor stderr includes remote seconds. The trusted publisher must still
validate every manifest/image before publication.

The stdin protocol is the ASCII line
`LETOVO_MAC_CI_V1 <unix-seconds> <run-id> <run-attempt> <source-sha>\n`, followed
by the Task 1 source tar, ending at EOF. Headers older than 300 seconds or more
than 30 seconds in the future are rejected. Identity must exactly match the
validated `request.json` inside the archive. Keep both machines' clocks synced.
SSH readiness is limited to 15 seconds, host execution to 1800 seconds, each
process-group shutdown to 30 seconds, and the GitHub job to 60 minutes.

Transport resource limits are enforced before writes or metadata expansion:

- Source archive: 256 MiB received, 256 MiB after gzip decompression, and
  256 MiB total logical file content. Only plain tar or gzip from the Task 1
  packer is accepted. Source headers are limited to 50,000 entries and each
  PAX record payload to 64 KiB; only path/time/owner metadata is accepted.
- Result archive: 2 GiB physical transfer on both the Mac and controller;
  each compressed image is at most 512 MiB, each manifest/report at most 1 MiB,
  and total logical content at most 2 GiB. Exactly nine required files and up
  to three directory entries are allowed. Result extended/PAX/GNU sparse
  headers are rejected before parser allocation, including tiny sparse tars.
- Guest results stream through `limactl shell <vm> -- cat
  /tmp/letovo-ci/result.tar` into a capped host file. The controller drains SSH
  stdout and stderr concurrently and caps result stdout before writing it.

While waiting for Lima, the supervisor drains both output streams and sends a
small stderr heartbeat every two seconds. A closed SSH channel therefore
cancels even a silent build without requiring a signal request to the remote
forced command. On broken pipe it redirects diagnostics to `/dev/null`, kills
only the active Lima process group, deletes the exact run VM/state, and releases
the global lock. Cleanup commands remain bounded; a failed VM deletion is
terminal and requires the exact-target manual cleanup described below.
If the channel first closes after VM deletion has begun, diagnostics are
silenced and the same deletion continues to its existing deadline. The lock and
run state stay held until that attempt finishes; channel loss remains terminal
even when deletion succeeds.

## Golden template and installed control

Use a dedicated non-admin Mac account `letovo-ci`, Lima **2.2.0**, macOS native
`/usr/bin/lockf`, Python **3.12.3**, and `/opt/homebrew/bin/limactl`. Native lockf
avoids GNU flock/coreutils requirements on macOS. `LIMACTL_BIN` is a trusted
installation override; `MAC_CI_STATE_DIR` defaults to
`/Users/letovo-ci/.local/state/letovo-ci`. The SSH service must not accept client
environment overrides for PATH, Python, BASH_ENV, Lima or these settings.
Client `AcceptEnv` must be off for this account; verify effective sshd settings
before activation. `MAC_CI_TEST_SOURCE_LIMIT`, `MAC_CI_TEST_RESULT_LIMIT`,
`MAC_CI_TEST_IMAGE_LIMIT` and `MAC_CI_TEST_LOGICAL_LIMIT` are trusted local test
injections only. They may lower the hard ceilings, never raise them; invalid or
nonpositive values fail closed. Do not set or forward them in production.

Install the approved commit's `mac-supervisor.sh`, `run-build.sh`,
`source_archive.py`, `image_manifest.py`, `build-bundle.sh`, `run-builder.sh`,
`builder_artifact.py`, and `build-builder.sh` together in
`/opt/letovo-ci/control`, owned by root:wheel (directories 0755, files 0644)
and unwritable by `letovo-ci`. The forced command below fixes PATH explicitly
to include pinned Python and `/opt/homebrew/bin`, without shell startup files.
Record the approved commit SHA and SHA-256 of each installed file. The
supervisor transfers these installed controls separately; source archives
cannot supply control scripts.

The `builder` protocol is separate from the four-image application bundle. It
accepts only `src/backend-builder.env` and `src/Dockerfile.builder`, builds and
checks one `linux/amd64` backend-builder image on the Mac, and returns it for
verification and GHCR publication by GitHub. The Mac still receives no registry
credentials. A main request first reuses an existing immutable `deps-<revision>`
tag when present; the publisher rechecks that tag before pushing a newly verified
artifact.

Create `letovo-ci-template` with a dedicated Ubuntu **24.04.3** disk image,
Docker Engine **28.4.0**, Buildx **0.27.0**, Node **22.19.0**, Python **3.12.3**,
pytest **8.4.2**, psycopg2-binary **2.9.10**, zstd **1.5.5**, GNU coreutils
**9.4** (`timeout`), Bash **5.2**, GNU tar **1.35**, and OpenSSH client/server.
These are provisioning version pins; actual installation and package
availability must be verified before activation. Record the downloaded guest
disk SHA-256 and full installed package versions in a root-owned template
inventory; do not provision from an unverified moving disk URL.

Use rootless Docker at `/run/user/<worker-uid>/docker.sock`; verify
`DOCKER_HOST=unix:///run/user/$(id -u)/docker.sock docker info` inside the guest.
The shim derives this socket from its guest UID and passes only that safe Docker
endpoint through the scrubbed environment. The worker user needs access to it. Guest execution is
untrusted, including Docker root-equivalent access. No SSH agent, host Docker
socket, shared home directory, cloud credential, production configuration, or
host directory mount is permitted. Set template `mounts: []`, disable automatic
host home mounts, and verify every clone uses `--mount-none`. Pre-create only
the empty `/tmp/letovo-ci` location with worker ownership. Guest SSH must
continue working without sharing the host filesystem.

Pre-pull/cache these inputs for `linux/amd64`:

- Backend builder at the exact digest from `src/backend-builder.lock` (currently
  `ghcr.io/letovo-dev/letovo-backend-builder@sha256:840cdc0a3236c763de27e3cc055321c56b546a1190829a58b1ea7d7ce6a49794`).
- Frontend base `node:22-alpine@sha256:968df39aedcea65eeb078fb336ed7191baf48f972b4479711397108be0966920`.
- `postgres:16`, `ubuntu:24.04`, and `python:3.11-slim`, at the exact digests
  recorded in the template inventory during provisioning. These three existing
  build inputs still use tags in Task 1/Dockerfiles; a cache inventory does not
  make subsequent online tag resolution immutable. Updating their source pins
  is a separate reviewed change, and rollout must record the resolved digests.

Check amd64 execution with the configured Lima architecture/emulation before
sealing; it must support `docker run --platform linux/amd64` and Buildx `--load`.
Warm caches, then remove all Docker/GHCR authentication files from every guest
home and `/root`, credential helpers, SSH private keys, shell histories and
provisioning tokens. Verify anonymous public image pulls. Stop the template;
never run builds in it. The supervisor refuses a running template and creates
`letovo-ci-<run-id>-<attempt>-<random>` for each accepted run. It copies files via
`limactl copy --backend=scp` into the VM, executes only fixed guest paths with `env -i`, and
deletes exactly its VM and temporary state after success, failure or timeout.
The supervisor opens fd 9 and acquires `lockf -s -t 0 9`, then execs Python;
this preserves the lock while delivering direct process signals to Python.
Non-PTY SSH channel loss is detected by the heartbeat rather than relying on
OpenSSH to signal the forced command.
Do not change this to `lockf FILE COMMAND`, whose waiter does not forward TERM.

## Network isolation required before activation

Enforce guest outbound filtering outside the guest, on its dedicated host
vmnet interface or upstream firewall. A guest-owned firewall is insufficient
because untrusted builds control guest Docker. Deny guest-initiated connections
to RFC1918 `10.0.0.0/8`, `172.16.0.0/12`, `192.168.0.0/16`, link-local
`169.254.0.0/16`, loopback, IPv6 ULA `fc00::/7` and link-local `fe80::/10`,
including the Mac/home LAN and metadata endpoints. Deny other home network
prefixes as applicable. Preserve established replies to host-initiated SSH/SCP
so source transfer and artifact return work.

Permit DNS only to the configured resolver on TCP/UDP 53, with a specific
exception if Lima DNS is private; this must not permit other resolver-host
ports. Permit required public HTTPS destinations for GitHub public sources,
GHCR/Docker Hub public image layers, npm and package mirrors. Configure IPv6
equivalent restrictions or disable guest IPv6. Packet-filter rules must cover
guest traffic before NAT, remain active across VM restarts, and fail closed if
the dedicated interface is absent. Record the actual interface, resolver,
installed rules and outbound-denial probes in rollout evidence. Do not enable
`MAC_CI_ENABLED` until these probes pass.

## Restricted SSH and reverse tunnel

Use a dedicated tunnel key on the Mac and a separate controller key in GitHub.
The controller public key is installed at both hops with different restrictions.
Replace the public-key placeholders below with actual public keys; never put
private keys in this repository.

Bastion `/home/letovo-ci-tunnel/.ssh/authorized_keys` (Mac-to-bastion key):

```text
restrict,port-forwarding,permitlisten="127.0.0.1:22222",command="/usr/bin/false" ssh-ed25519 TUNNEL_PUBLIC_KEY
```

Bastion `/home/letovo-ci-proxy/.ssh/authorized_keys` (GitHub controller key):

```text
restrict,port-forwarding,permitopen="127.0.0.1:22222",command="/usr/bin/false" ssh-ed25519 CONTROLLER_PUBLIC_KEY
```

Mac `/Users/letovo-ci/.ssh/authorized_keys` (same controller public key):

```text
restrict,command="/usr/bin/env PATH=/opt/homebrew/bin:/usr/bin:/bin:/usr/sbin:/sbin /bin/bash /opt/letovo-ci/control/mac-supervisor.sh" ssh-ed25519 CONTROLLER_PUBLIC_KEY
```

In bastion sshd configuration keep `GatewayPorts no`. For the tunnel account
set `AllowTcpForwarding remote`, `PermitListen 127.0.0.1:22222`,
`PermitOpen none`, `MaxSessions 0`; for the proxy account set
`AllowTcpForwarding local`, `PermitOpen 127.0.0.1:22222`, `PermitListen none`,
`MaxSessions 0`. Both accounts require `AllowAgentForwarding no`, `PermitTTY no`,
`X11Forwarding no`, and `AllowStreamLocalForwarding no`. Validate with `sshd -t`
and `sshd -T -C user=<account>,host=<host>,addr=<address>` before reload. These
settings forbid shells while allowing the dedicated `ssh -N`/ProxyJump paths.

Pin the bastion host key from a trusted console in
`/Users/letovo-ci/.ssh/known_hosts`; do not trust an unauthenticated ssh-keyscan.
Install this launchd plist at
`/Library/LaunchDaemons/ru.letovo.ci-tunnel.plist`, owned by root:wheel and mode
0644, to start without screen login. The daemon runs as the dedicated user:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>Label</key><string>ru.letovo.ci-tunnel</string>
  <key>UserName</key><string>letovo-ci</string>
  <key>ProgramArguments</key><array>
    <string>/usr/bin/ssh</string><string>-NT</string>
    <string>-i</string><string>/Users/letovo-ci/.ssh/ci-tunnel</string>
    <string>-o</string><string>BatchMode=yes</string>
    <string>-o</string><string>IdentitiesOnly=yes</string>
    <string>-o</string><string>StrictHostKeyChecking=yes</string>
    <string>-o</string><string>ExitOnForwardFailure=yes</string>
    <string>-o</string><string>ServerAliveInterval=15</string>
    <string>-o</string><string>ServerAliveCountMax=3</string>
    <string>-o</string><string>ConnectTimeout=15</string>
    <string>-R</string><string>127.0.0.1:22222:127.0.0.1:22</string>
    <string>letovo-ci-tunnel@ya.sergeiscv.ru</string>
  </array>
  <key>RunAtLoad</key><true/><key>KeepAlive</key><true/>
  <key>ThrottleInterval</key><integer>15</integer>
  <key>StandardErrorPath</key><string>/Users/letovo-ci/Library/Logs/ci-tunnel.log</string>
</dict></plist>
```

Load with `sudo launchctl bootstrap system /Library/LaunchDaemons/ru.letovo.ci-tunnel.plist`
after approval. Use `sudo launchctl print system/ru.letovo.ci-tunnel` for state.
Verify private-key mode 0600, `.ssh` mode 0700 and dedicated-user ownership;
pre-create its Logs directory with that ownership.
Confirm bastion `ss -ltn sport = :22222` shows only `127.0.0.1:22222`.

## GitHub configuration, probes and rollback

Repository secrets: `MAC_CI_SSH_KEY` holds only the controller private key;
`MAC_CI_KNOWN_HOSTS` contains independently verified entries for
`ya.sergeiscv.ru` and the alias `letovo-ci-worker`. The controller materializes
them as mode-0600 files and exports `MAC_CI_SSH_KEY_FILE` and
`MAC_CI_KNOWN_HOSTS_FILE`; neither file enters the source archive.

Repository variables: `MAC_CI_ENABLED` (only literal `false` selects hosted),
`MAC_CI_BASTION_HOST=ya.sergeiscv.ru`, `MAC_CI_BASTION_USER=letovo-ci-proxy`,
`MAC_CI_WORKER_USER=letovo-ci`, `MAC_CI_WORKER_PORT=22222`. `SSH_BIN` is a local
test override, not a repository secret. Use `timeout-minutes: 60` on controllers.
No package write token, GitHub token, submodule/deploy key or production secret
is passed to Lima or the worker. ProxyJump creates its own forwarding child;
the controller makes one top-level SSH invocation, without retries.

Before rollout, run the transport pytest file, then approved build-only pilots:
normal; `MAC_CI_ENABLED=false`; tunnel unavailable; supervisor lock held; worker
failure after marker; timeout/cancellation; closed output channels during a
silent build; stale identity; absent/truncated/sparse/oversized results. Verify
one hosted fallback only for disabled/offline/busy, failure after
start, exact manifests and architecture, empty guest Docker auth directories,
network denials, and no remaining run VM/state. A result archive alone is not
successful publication or successful deployment evidence.

For an explicit busy probe, hold the same native lock with
`lockf -s -t 0 -k /Users/letovo-ci/.local/state/letovo-ci/global.lock sleep 30`
during a build-only pilot. Review the exact target of any cleanup from
`limactl list --json` and the run summary. Delete only an identified abandoned
`letovo-ci-<id>-<attempt>-<random>` VM using `limactl delete --force <exact-name>`;
remove only its identified state directory after confirming no worker is alive.
Never delete the template, all Lima instances, or directories through a glob.

Rollback is `MAC_CI_ENABLED=false`, then wait for the active run to clean up.
Disabling future dispatch does not cancel an already-started build. Stop the
launchd service only after draining active work. Rotate controller/tunnel keys
independently: install restricted new public key, update its client secret,
verify pinned handshake and forbidden forwarding/shell probes, then remove the
old public key. For host-key rotation verify the replacement out of band and
update both pinned known-hosts locations before reconnecting. Upgrade the
template/control inventory through a reviewed change, preserving the previous
stopped template for rollback until a new approved pilot succeeds.
