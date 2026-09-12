"""Parsed trust-boundary contracts and executable controller shell checks."""
import io
import hashlib
import json
import os
import re
import subprocess
import tarfile
from pathlib import Path

import pytest
import yaml

ROOT = Path(__file__).resolve().parents[1]
CONTEXTS = ["backend-pr", "frontend-verify", "live-deployment-e2e"]


def workflow(name):
    path = ROOT / ".github/workflows" / name
    assert path.exists(), f"missing trusted workflow: {name}"
    return yaml.safe_load(path.read_text())


def step(job, name):
    return next(s for s in job["steps"] if s.get("name") == name)


def controller():
    return workflow("mac-ci-pr-controller.yml")


def test_active_request_trigger_and_permissions():
    doc = controller()
    assert doc.get("on", doc.get(True)) == {"workflow_run": {"workflows": ["Mac CI request"], "types": ["completed"]}}
    assert doc["permissions"] == {"contents": "read"}
    request = workflow("pr-ci-request.yml")
    assert request.get("on", request.get(True)) == {"pull_request": {"branches": ["main"]}}
    assert request["permissions"] == {"contents": "read"}
    assert list(request["jobs"]) == ["complete"]
    assert request["jobs"]["complete"]["permissions"] == {"contents": "read"}
    assert "secrets." not in json.dumps(request)
    for name, job in doc["jobs"].items():
        permissions = job["permissions"]
        assert permissions.get("contents") == "read"
        assert {k for k, v in permissions.items() if v == "write"} <= (
            {"packages"} if name == "publish" else {"statuses"} if name in {"pending", "finalize"} else set())
    assert doc["jobs"]["hosted"]["permissions"] == {"contents": "read"}
    assert doc["jobs"]["fetch-builder"]["permissions"] == {"contents": "read", "packages": "read"}


def test_every_action_is_pinned_to_a_full_commit():
    for doc in [controller(), workflow("mac-ci-pilot.yml"), workflow("docker-image.yml"), workflow("production-release.yml")]:
        for job in doc["jobs"].values():
            for action in job["steps"]:
                if "uses" in action:
                    assert re.fullmatch(r"[\w-]+/[\w-]+@[0-9a-f]{40}", action["uses"])


def test_main_uses_mac_first_bundle_and_publisher_only_push():
    doc = workflow("docker-image.yml")
    assert doc.get("on", doc.get(True)) == {"push": {"branches": ["main"]}}
    assert doc["concurrency"] == {"group": "build-and-verify-main", "cancel-in-progress": False}
    jobs = doc["jobs"]
    assert set(jobs) == {"resolve", "mac", "fetch-builder", "hosted", "publish-main"}
    assert jobs["mac"]["timeout-minutes"] == 60
    assert "75) echo \"fallback=true\"" in step(jobs["mac"], "Try Mac")["run"]
    assert "needs.mac.outputs.fallback == 'true'" in jobs["fetch-builder"]["if"]
    assert "needs.mac.outputs.fallback == 'true'" in jobs["hosted"]["if"]
    assert len([s for s in jobs["hosted"]["steps"] if "build-bundle.sh" in s.get("run", "")]) == 1
    publisher = jobs["publish-main"]
    assert publisher["permissions"] == {"contents": "read", "packages": "write"}
    assert "always()" in publisher["if"]
    assert step(publisher, "Download exact result")["with"]["name"] == "${{ needs.resolve.outputs.artifact }}"
    text = json.dumps(publisher)
    assert "publish-bundle.sh" in text and " main --publish-only" in text
    assert 'profile="production"' in step(jobs["resolve"], "Freeze request")["run"]
    assert 'artifact=f"letovo-images-{request[\'run_id\']}-{request[\'run_attempt\']}-production-{source_sha}"' in step(jobs["resolve"], "Freeze request")["run"]
    assert "build-bundle.sh" not in text and "build-push-action" not in text and "docker build " not in text
    assert "docker push" not in json.dumps({name: job for name, job in jobs.items() if name != "publish-main"})


def test_main_freezes_source_and_public_frontend_without_submodule_secret():
    jobs = workflow("docker-image.yml")["jobs"]
    resolve = jobs["resolve"]
    assert set(resolve["outputs"]) >= {"source_sha", "frontend_gitlink", "control_sha", "request", "artifact"}
    freeze = step(resolve, "Freeze request")["run"]
    assert "git/trees/" in freeze
    assert 'api("branches/main")["commit"]["sha"] == source_sha' in freeze
    assert resolve["steps"][-1]["env"]["PRODUCTION_BASE_URL"] == "${{ vars.PRODUCTION_BASE_URL || 'https://letovocorp.ru' }}"
    assert 'base_url=os.environ["PRODUCTION_BASE_URL"]' in freeze
    mac = jobs["mac"]
    assert step(mac, "Checkout trusted controls")["with"]["path"] == "control"
    assert step(mac, "Checkout frozen source")["with"]["ref"] == "${{ needs.resolve.outputs.source_sha }}"
    frontend = step(mac, "Checkout public frontend")["with"]
    assert frontend["repository"] == "letovo-dev/letovo-all-frontend"
    assert frontend["ref"] == "${{ needs.resolve.outputs.frontend_gitlink }}"
    assert "SUBMODULE_SSH_KEY" not in json.dumps(jobs)
    assert "secrets." not in json.dumps(jobs["hosted"])
    assert jobs["hosted"]["permissions"] == {"contents": "read"}


def test_main_resolver_emits_frozen_production_request(tmp_path):
    script = step(workflow("docker-image.yml")["jobs"]["resolve"], "Freeze request")["run"]
    (tmp_path / "control").symlink_to(ROOT, target_is_directory=True)
    source_sha, frontend_sha = "a" * 40, "b" * 40
    responses = {
        f"commits/{source_sha}": {"sha": source_sha},
        "branches/main": {"commit": {"sha": source_sha}},
        f"git/trees/{source_sha}": {
            "tree": [{"path": "frontend", "mode": "160000", "type": "commit", "sha": frontend_sha}]
        },
    }
    (tmp_path / "responses").write_text(json.dumps(responses))
    fake = tmp_path / "gh"
    fake.write_text('#!/usr/bin/env python3\nimport json,os,sys\nprint(json.dumps(json.load(open(os.environ["RESPONSES"]))[sys.argv[-1].removeprefix("repos/letovo-dev/letovo-all/")]))\n')
    fake.chmod(0o755)
    fake = tmp_path / "git"
    fake.write_text('#!/bin/sh\nprintf "%s\\n" "$CONTROL_SHA"\n')
    fake.chmod(0o755)
    output = tmp_path / "output"
    env = {
        **os.environ,
        "PATH": f"{tmp_path}:{os.environ['PATH']}",
        "CONTROL_SHA": source_sha,
        "BUILD_FILES": "basic/auth.cc",
        "PRODUCTION_BASE_URL": "https://school.example",
        "BUILDER_IMAGE": "ghcr.io/letovo-dev/letovo-backend-builder@sha256:" + "f" * 64,
        "GITHUB_OUTPUT": str(output),
        "GITHUB_RUN_ID": "123",
        "GITHUB_RUN_ATTEMPT": "2",
        "RESPONSES": str(tmp_path / "responses"),
    }
    result = subprocess.run(["bash", "-e", "-c", script], cwd=tmp_path, env=env, capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    outputs = dict(line.split("=", 1) for line in output.read_text().splitlines())
    request = json.loads(outputs["request"])
    assert request["job"] == "main" and request["profile"] == "production"
    assert request["base_url"] == "https://school.example"
    assert request["source_sha"] == source_sha and request["frontend_gitlink"] == frontend_sha
    assert outputs["artifact"] == f"letovo-images-123-2-production-{source_sha}"


def test_production_release_builds_frozen_main_bundle_before_protected_deploy():
    doc = workflow("production-release.yml")
    jobs = doc["jobs"]
    assert set(jobs) == {
        "preview-child-avatar-migration",
        "build-release-images",
        "fetch-release-builder",
        "hosted-release-images",
        "release",
    }

    preview = json.dumps(jobs["preview-child-avatar-migration"])
    for forbidden in ["build-bundle.sh", "publish-bundle.sh", "docker build", "build-push-action"]:
        assert forbidden not in preview

    build = jobs["build-release-images"]
    assert build["if"] == "inputs.child_avatar_migration_mode == 'apply'"
    assert build["permissions"] == {"contents": "read"}
    assert build["timeout-minutes"] == 60
    assert set(build["outputs"]) >= {
        "source_sha", "frontend_gitlink", "request", "artifact", "builder_image", "fallback", "source_sha256"
    }
    main = step(build, "Resolve frozen main")["run"]
    assert "repos/letovo-dev/letovo-all/branches/main" in main
    freeze = step(build, "Freeze release request")["run"]
    assert 'api("branches/main")' not in freeze
    assert 'api("commits/" + source_sha)["sha"] == source_sha' in freeze
    assert '["git", "-C", "control", "rev-parse", "HEAD"]' in freeze
    assert 'job="release"' in freeze and 'profile="production"' in freeze
    assert 'os.environ["PRODUCTION_BASE_URL"] != "https://letovocorp.ru"' in freeze
    assert 'artifact=f"letovo-images-{request[\'run_id\']}-{request[\'run_attempt\']}-production-{source_sha}"' in freeze
    assert step(build, "Checkout frozen source")["with"]["ref"] == "${{ steps.freeze.outputs.source_sha }}"
    frontend = step(build, "Checkout public frontend")["with"]
    assert frontend["repository"] == "letovo-dev/letovo-all-frontend"
    assert frontend["ref"] == "${{ steps.freeze.outputs.frontend_gitlink }}"
    assert "75) echo \"fallback=true\"" in step(build, "Try Mac")["run"]
    assert "secrets.MAC_CI_SSH_KEY" in json.dumps(build)
    assert "LETOVO_PROD_" not in json.dumps(build) and "SUBMODULE_SSH_KEY" not in json.dumps(build)

    fetch = jobs["fetch-release-builder"]
    assert fetch["permissions"] == {"contents": "read", "packages": "read"}
    assert "needs.build-release-images.outputs.fallback == 'true'" in fetch["if"]
    hosted = jobs["hosted-release-images"]
    assert hosted["permissions"] == {"contents": "read"}
    assert "needs.build-release-images.outputs.fallback == 'true'" in hosted["if"]
    assert len([s for s in hosted["steps"] if "build-bundle.sh" in s.get("run", "")]) == 1
    assert "secrets." not in json.dumps(hosted)

    release = jobs["release"]
    assert set(release["needs"]) == {"build-release-images", "hosted-release-images"}
    assert "always()" in release["if"] and "inputs.child_avatar_migration_mode == 'apply'" in release["if"]
    assert release["environment"] == "production"
    assert release["permissions"] == {"contents": "read", "packages": "write"}
    assert step(release, "Download exact release bundle")["with"]["name"] == "${{ needs.build-release-images.outputs.artifact }}"
    assert step(release, "Checkout frozen release source")["with"]["ref"] == "${{ needs.build-release-images.outputs.source_sha }}"
    release_text = json.dumps(release)
    for forbidden in ["build-bundle.sh", "build-push-action", "docker build "]:
        assert forbidden not in release_text
    assert "publish-bundle.sh" in release_text and " release --publish-only" in release_text
    names = [s.get("name") for s in release["steps"]]
    assert names.index("Validate production deployment secrets") < names.index("Publish verified release images")
    assert names.index("Publish verified release images") < names.index("Configure production SSH")


def test_resolve_freezes_run_pr_merge_and_trusted_request():
    job = controller()["jobs"]["resolve"]
    script = step(job, "Freeze request")["run"]
    for marker in [".github/workflows/pr-ci-request.yml", "pull_request", "success", "letovo-dev/letovo-all", "head_sha", "merge_commit_sha", "pulls/", "commits/", "git/trees/", "160000"]:
        assert marker in script
    assert set(job["outputs"]) >= {"head_sha", "source_sha", "frontend_gitlink", "control_sha", "request", "artifact", "pr_number"}
    assert "len(" in script
    assert step(job, "Checkout trusted controls")["with"]["ref"] == "${{ github.sha }}"


def test_trusted_builder_contract_gates_candidate_before_pending_and_mac():
    jobs = controller()["jobs"]
    validation = jobs["source-validation"]
    assert validation["needs"] == ["resolve"]
    assert validation["permissions"] == {"contents": "read"}
    controls = step(validation, "Checkout trusted controls")["with"]
    candidate = step(validation, "Checkout frozen source")["with"]
    assert controls == {
        "ref": "${{ needs.resolve.outputs.control_sha }}",
        "path": "control",
        "persist-credentials": False,
    }
    assert candidate == {
        "ref": "${{ needs.resolve.outputs.source_sha }}",
        "path": "candidate",
        "persist-credentials": False,
    }
    check = step(validation, "Validate frozen backend builder contract")
    assert check["env"] == {"LETOVO_CONTRACT_ROOT": "${{ github.workspace }}/candidate"}
    assert check["run"] == "python3 control/test/test_issue215_backend_builder_contract.py"
    assert "candidate/test" not in json.dumps(validation)
    assert "secrets." not in json.dumps(validation) and "packages" not in validation["permissions"]
    assert jobs["pending"]["needs"] == ["resolve", "source-validation"]
    assert jobs["mac"]["needs"] == ["resolve", "pending", "source-validation"]
    assert "source-validation" in jobs["finalize"]["needs"]
    assert "SOURCE_VALIDATION_RESULT" in step(jobs["finalize"], "Report terminal statuses")["env"]


@pytest.mark.parametrize(
    "mutation",
    [
        ("run: python3 control/test/test_issue215_backend_builder_contract.py", "run: python3 control/test/test_issue215_backend_builder_contract.py && true"),
        ("    - name: Validate frozen backend builder contract\n      env:\n", "    - name: Validate frozen backend builder contract\n      if: always()\n      env:\n"),
        ("    - name: Validate frozen backend builder contract\n      env:\n", "    - name: Validate frozen backend builder contract\n      continue-on-error: true\n      env:\n"),
        ("${{ github.workspace }}/candidate", "${{ github.workspace }}/candidate-evil"),
        ("run: python3 control/", "run: python3 -O control/"),
        ("permissions:\n      contents: read", "env:\n      PYTHONOPTIMIZE: 1\n    permissions:\n      contents: read"),
    ],
)
def test_trusted_builder_contract_self_check_rejects_wiring_mutations(mutation):
    from test_issue215_backend_builder_contract import assert_pr_controller_validation

    source = (ROOT / ".github/workflows/mac-ci-pr-controller.yml").read_text()
    old, new = mutation
    assert old in source
    with pytest.raises(AssertionError):
        assert_pr_controller_validation(source.replace(old, new, 1))


def test_candidate_checkout_and_pack_are_separate_from_controls():
    job = controller()["jobs"]["mac"]
    controls = step(job, "Checkout trusted controls")["with"]
    candidate = step(job, "Checkout frozen source")["with"]
    frontend = step(job, "Checkout public frontend")["with"]
    assert controls["path"] == "control"
    assert controls["ref"] == "${{ needs.resolve.outputs.control_sha }}"
    assert candidate["path"] == "candidate"
    assert candidate["ref"] == "${{ needs.resolve.outputs.source_sha }}"
    assert frontend["repository"] == "letovo-dev/letovo-all-frontend"
    assert frontend["ref"] == "${{ needs.resolve.outputs.frontend_gitlink }}"
    for checkout in [controls, candidate, frontend]:
        assert checkout["persist-credentials"] is False
        assert not checkout.get("submodules")
        assert "ssh-key" not in checkout
    script = step(job, "Pack frozen source")["run"]
    assert "control/scripts/export_backend_builder.sh" in script
    assert "control/src/backend-builder.lock" in script
    assert "control/scripts/ci/source_archive.py" in script
    assert "extract" in step(controller()["jobs"]["hosted"], "Extract frozen source")["run"]
    text = json.dumps(job)
    assert "SUBMODULE_SSH_KEY" not in text and "PROD_" not in text


def test_mac_classification_credentials_and_one_fallback():
    for doc in [controller(), workflow("mac-ci-pilot.yml")]:
        job = doc["jobs"]["mac"]
        assert job["timeout-minutes"] == 60
        mac = step(job, "Try Mac")
        script = mac["run"]
        assert "umask 077" in script and "chmod 600" in script
        assert "trap" in script and "rm -rf" in script
        assert "0)" in script and "75)" in script and '*) exit "$status"' in script
        hosted = doc["jobs"]["hosted"]
        fetch = doc["jobs"]["fetch-builder"]
        assert len([s for s in hosted["steps"] if "build-bundle.sh" in s.get("run", "")]) == 1
        pull = step(fetch, "Export private builder")
        fallback = step(hosted, "Hosted fallback")
        assert "needs.mac.outputs.fallback == 'true'" in fetch["if"]
        assert "needs.mac.outputs.fallback == 'true'" in hosted["if"]
        assert "DOCKER_CONFIG" in pull["run"] and "trap" in pull["run"] and "rm -rf" in pull["run"]
        assert "docker pull --platform linux/amd64" in pull["run"]
        assert "docker buildx use default" in fallback["run"]
        assert not fallback.get("env", {}).get("GH_TOKEN")
        assert "secrets." not in json.dumps(fallback)
        artifact = step(job, "Upload result")["with"]
        assert artifact["compression-level"] == 0 and artifact["retention-days"] <= 3
        assert artifact["path"] == "${{ runner.temp }}/result.tar"


def test_publisher_reconstructs_expected_and_never_builds():
    job = controller()["jobs"]["publish"]
    assert job["needs"] == ["resolve", "mac", "hosted"]
    assert "always()" in job["if"]
    assert step(job, "Download exact result")["with"]["name"] == "${{ needs.resolve.outputs.artifact }}"
    assert step(job, "Recreate expected request")["env"]["EXPECTED_JSON"] == "${{ needs.resolve.outputs.request }}"
    text = json.dumps(job)
    assert "build-bundle.sh" not in text and "build-push-action" not in text and "docker build " not in text
    assert "publish-bundle.sh" in text and "candidate" in text
    extraction = step(job, "Extract bounded result")["run"]
    assert "extractall" not in extraction
    assert "result_archive.py" in extraction


def test_status_and_deploy_failure_propagation():
    jobs = controller()["jobs"]
    assert jobs["mac"]["needs"] == ["resolve", "pending", "source-validation"]
    final = jobs["finalize"]
    assert "always()" in final["if"]
    assert set(final["needs"]) == {"resolve", "source-validation", "pending", "mac", "fetch-builder", "hosted", "publish", "deploy"}
    script = step(final, "Report terminal statuses")["run"]
    for context in CONTEXTS:
        assert context in script
    assert "success" in script and "failure" in script
    deploy = jobs["deploy"]
    assert deploy["if"] == (
        "always() && needs.resolve.result == 'success' && needs.publish.result == 'success'"
    )
    assert deploy["concurrency"] == {"group": "live-deployment-e2e", "cancel-in-progress": False}
    deploy_script = step(deploy, "Deploy PR candidate images to live e2e")["run"]
    restore_script = step(deploy, "Restore live deployment images")["run"]
    for migration in [
        "avatar_upload_role_migration.sql",
        "roles_natural_key_migration.sql",
        "child_avatar_access_migration.sql",
        "department_payout_migration.sql",
        "publisher_authorization_migration.sql",
        "post_media_order_migration.sql",
    ]:
        assert f"control/docs/{migration}" in deploy_script
        assert f'"$state_dir/{migration}"' in deploy_script
    for backup in [
        "post-media.before-order-migration.sql",
        "transactions.before-department-payout-migration.sql",
        "role.before-avatar-migration.sql",
        "roles.before-natural-key-migration.sql",
        "user.before-child-avatar-migration.sql",
        "child-avatar-migration-preview.csv",
    ]:
        assert backup in deploy_script
    for service in ["letovo-server", "letovo-registration-server", "letovo-front", "flask-uploader"]:
        assert f"{service}={{{{.Config.Image}}}}" in deploy_script
        assert service in restore_script
    assert deploy_script.index("post-media.before-order-migration.sql") < deploy_script.index("-f /tmp/post_media_order_migration.sql")
    assert deploy_script.index("transactions.before-department-payout-migration.sql") < deploy_script.index("-f /tmp/department_payout_migration.sql")
    assert "-v apply=false" in deploy_script and "-v apply=true" in deploy_script
    assert "docker compose" in deploy_script and "docker compose" in restore_script
    assert "pull letovo-server letovo-registration-server letovo-front letovo-flask-uploader || true" in restore_script
    assert 'rm -rf "$state_dir"' in restore_script
    assert step(deploy, "Run live browser smoke")["run"] == "node $GITHUB_WORKSPACE/control/test/e2e/live-platform-smoke.mjs"
    assert step(deploy, "Restore live deployment images")["if"] == "always()"


def test_pilot_modes_and_no_publication():
    doc = workflow("mac-ci-pilot.yml")
    trigger = doc.get("on", doc.get(True))
    assert set(trigger) == {"workflow_dispatch"}
    assert trigger["workflow_dispatch"]["inputs"]["mode"]["options"] == ["normal", "disabled", "offline", "busy", "remote-failure"]
    assert "github.ref == 'refs/heads/main'" in doc["jobs"]["resolve"]["if"]
    text = json.dumps(doc)
    assert "production" in text and "pilot" in text
    for forbidden in ["publish-bundle.sh", "docker push", "LETOVO_E2E", "report-status.sh", '"path": "candidate"']:
        assert forbidden not in text
    assert all("write" not in j["permissions"].values() for j in doc["jobs"].values())


def test_pilot_uses_real_fault_paths():
    jobs = workflow("mac-ci-pilot.yml")["jobs"]
    script = step(jobs["mac"], "Try Mac")["run"]
    assert "SSH_BIN" not in script
    assert "letovo-ci-offline.invalid" in script
    assert "normal/0|disabled/75|offline/75|busy/75" in script
    assert 'remote-failure/*) exit "$status"' in script
    assert "mac-ci-injected-missing.cc" in step(jobs["resolve"], "Freeze request")["run"]


@pytest.mark.parametrize("mutation", [None, "path", "event", "conclusion", "fork", "stale", "multiple", "closed", "merge", "gitlink"])
def test_resolver_rejects_untrusted_or_stale_api_context(tmp_path, mutation):
    script = step(controller()["jobs"]["resolve"], "Freeze request")["run"]
    (tmp_path / "control").symlink_to(ROOT, target_is_directory=True)
    head, merge, base, control, frontend = [c * 40 for c in "abcde"]
    repo = {"full_name": "letovo-dev/letovo-all"}
    run = dict(name="Mac CI request", path=".github/workflows/pr-ci-request.yml", event="pull_request", conclusion="success", repository=repo, head_repository=repo, head_sha=head, pull_requests=[{"number": 214, "head": {"sha": head}}])
    pr = dict(state="open", merged=False, head={"repo": repo, "sha": head}, base={"repo": repo, "sha": base, "ref": "main"}, merge_commit_sha=merge)
    tree = {"tree": [{"path": "frontend", "mode": "160000", "type": "commit", "sha": frontend}]}
    commits = {"sha": merge, "parents": [{"sha": base}, {"sha": head}]}
    if mutation in {"path", "event", "conclusion"}:
        run[mutation] = "evil"
    elif mutation == "fork":
        pr["head"]["repo"] = {"full_name": "attacker/fork"}
    elif mutation == "stale":
        pr["head"]["sha"] = "f" * 40
    elif mutation == "multiple":
        run["pull_requests"] *= 2
    elif mutation == "closed":
        pr["state"] = "closed"
    elif mutation == "merge":
        commits["parents"] = [{"sha": base}]
    elif mutation == "gitlink":
        tree["tree"][0]["mode"] = "100644"
    (tmp_path / "event").write_text(json.dumps({"workflow_run": run}))
    (tmp_path / "responses").write_text(json.dumps({"actions/runs/99": run, "pulls/214": pr, f"commits/{merge}": commits, f"git/trees/{merge}": tree}))
    fake = tmp_path / "gh"
    fake.write_text('#!/usr/bin/env python3\nimport json,os,sys\nprint(json.dumps(json.load(open(os.environ["RESPONSES"]))[sys.argv[-1].removeprefix("repos/letovo-dev/letovo-all/")]))\n')
    fake.chmod(0o755)
    fake = tmp_path / "git"
    fake.write_text('#!/bin/sh\nprintf "%s\\n" "$CONTROL_SHA"\n')
    fake.chmod(0o755)
    env = {**os.environ, "PATH": f"{tmp_path}:{os.environ['PATH']}", "CONTROL_SHA": control, "REQUEST_RUN": "99", "BUILD_FILES": "basic/auth.cc", "BUILDER_IMAGE": "ghcr.io/letovo-dev/letovo-backend-builder@sha256:" + "f" * 64, "GITHUB_EVENT_PATH": str(tmp_path / "event"), "GITHUB_OUTPUT": str(tmp_path / "output"), "RESPONSES": str(tmp_path / "responses"), "GITHUB_RUN_ID": "123", "GITHUB_RUN_ATTEMPT": "2"}
    result = subprocess.run(["bash", "-e", "-c", script], cwd=tmp_path, env=env, capture_output=True, text=True)
    if mutation:
        assert result.returncode != 0
        if (tmp_path / "output").exists():
            assert "request=" not in (tmp_path / "output").read_text()
    else:
        assert result.returncode == 0, result.stderr
        outputs = dict(line.split("=", 1) for line in (tmp_path / "output").read_text().splitlines())
        request = json.loads(outputs["request"])
        assert request["source_sha"] == merge and outputs["head_sha"] == head
        assert request["frontend_gitlink"] == frontend and outputs["control_sha"] == control
        assert outputs["artifact"] == f"letovo-images-123-2-candidate-{merge}"


@pytest.mark.parametrize("status", [0, 75, 1, 2])
def test_mac_step_scrubs_ssh_secrets_on_all_outcomes(tmp_path, status):
    script = step(controller()["jobs"]["mac"], "Try Mac")["run"]
    controls = tmp_path / "control/scripts/ci"
    controls.mkdir(parents=True)
    (controls / "try-mac.sh").write_text('set -eu\ntest -z "${MAC_CI_SSH_KEY:-}"\ntest -z "${MAC_CI_KNOWN_HOSTS:-}"\ntest "$(stat -c %a "$MAC_CI_SSH_KEY_FILE")" = 600\ntest "$(stat -c %a "$MAC_CI_KNOWN_HOSTS_FILE")" = 600\nprintf "executor=test\\n" > "$4"\nexit ' + str(status) + '\n')
    env = {**os.environ, "RUNNER_TEMP": str(tmp_path), "MAC_CI_SSH_KEY": "fake-private-key", "MAC_CI_KNOWN_HOSTS": "fake-host-key", "GITHUB_OUTPUT": str(tmp_path / "output"), "GITHUB_STEP_SUMMARY": str(tmp_path / "summary")}
    result = subprocess.run(["bash", "-e", "-c", script], cwd=tmp_path, env=env, capture_output=True, text=True)
    assert result.returncode == (0 if status in (0, 75) else status), result.stderr
    assert not list(tmp_path.glob("mac-auth.*"))
    if status in (0, 75):
        assert (tmp_path / "output").read_text() == f"fallback={str(status == 75).lower()}\n"


@pytest.mark.parametrize("mutation", [None, "extra", "missing", "symlink", "sparse", "duplicate", "size", "truncated"])
def test_publisher_tar_extraction_is_bounded_and_exact(tmp_path, mutation):
    script = step(controller()["jobs"]["publish"], "Extract bounded result")["run"]
    (tmp_path / "control").symlink_to(ROOT, target_is_directory=True)
    download = tmp_path / "download"
    download.mkdir()
    names = ["manifest.json"] + [f"{d}/{n}.{e}" for n in ("backend", "registration", "frontend", "uploader") for d, e in (("images", "tar.zst"), ("reports", "json"))]
    if mutation == "extra":
        names.append("../escape")
    if mutation == "missing":
        names.pop()
    if mutation == "duplicate":
        names.append(names[0])
    archive = download / "result.tar"
    with tarfile.open(archive, "w", format=tarfile.USTAR_FORMAT) as tar:
        for index, name in enumerate(names):
            member = tarfile.TarInfo(name)
            member.size = 2
            if index == 0 and mutation in ("symlink", "sparse"):
                member.type = tarfile.SYMTYPE if mutation == "symlink" else tarfile.GNUTYPE_SPARSE
                member.linkname = "/tmp/escape"
                member.size = 0
            tar.addfile(member, io.BytesIO(b"{}"))
    if mutation == "size":
        with archive.open("r+b") as stream:
            member = tarfile.TarInfo("manifest.json")
            member.size = 1024**3
            stream.write(member.tobuf())
    if mutation == "truncated":
        archive.write_bytes(archive.read_bytes()[:-1])
    result = subprocess.run(["bash", "-e", "-c", script], cwd=tmp_path, env={**os.environ, "RUNNER_TEMP": str(tmp_path)}, capture_output=True, text=True)
    if mutation:
        assert result.returncode != 0
        assert not (tmp_path / "verified").exists()
    else:
        assert result.returncode == 0, result.stderr
        assert {p.relative_to(tmp_path / "verified").as_posix() for p in (tmp_path / "verified").rglob("*") if p.is_file()} == set(names)


@pytest.mark.parametrize("context,state", [(c, s) for c in CONTEXTS for s in ["pending", "success", "failure", "error"]])
def test_status_api_exact_target(tmp_path, context, state):
    script = ROOT / "scripts/ci/report-status.sh"
    assert script.exists(), "missing status reporter"
    fake = tmp_path / "gh"
    fake.write_text('#!/usr/bin/env python3\nimport json,os,sys\nopen(os.environ["CALL"],"w").write(json.dumps(sys.argv[1:]))\n')
    fake.chmod(0o755)
    env = {**os.environ, "PATH": f"{tmp_path}:{os.environ['PATH']}", "CALL": str(tmp_path / "call"), "GITHUB_REPOSITORY": "letovo-dev/letovo-all", "GITHUB_RUN_ID": "123", "GH_TOKEN": "fake", "PR_CONTROLLER": "true"}
    result = subprocess.run(["bash", str(script), "a" * 40, context, state], env=env, capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    args = json.loads((tmp_path / "call").read_text())
    assert f"repos/letovo-dev/letovo-all/statuses/{'a' * 40}" in args
    assert f"context={context}" in args and f"state={state}" in args


@pytest.mark.parametrize("args", [["a" * 40, "evil", "success"], ["a" * 40, "backend-pr", "skipped"], ["main", "backend-pr", "success"]])
def test_status_rejects_invalid_input_before_api(args):
    script = ROOT / "scripts/ci/report-status.sh"
    assert script.exists(), "missing status reporter"
    result = subprocess.run(["bash", str(script), *args], env={**os.environ, "PR_CONTROLLER": "true"}, capture_output=True)
    assert result.returncode == 2


def test_candidate_execution_has_no_privileged_job_context():
    for doc in [controller(), workflow("mac-ci-pilot.yml")]:
        assert "secrets." not in json.dumps(doc.get("env", {}))
        for name, job in doc["jobs"].items():
            text = json.dumps(job)
            if "build-bundle.sh" in text:
                assert name == "hosted"
                assert "secrets." not in text
                assert "packages" not in job["permissions"]
                assert "docker login" not in text and "GH_TOKEN" not in text
                assert "MAC_CI_SSH" not in text and "MAC_CI_KNOWN_HOSTS" not in text
            if name != "hosted":
                assert "pip install" not in text and "apt-get" not in text or name == "publish"
            if name == "mac":
                assert "actions/setup-node" not in text and "docker login" not in text
        hosted = doc["jobs"]["hosted"]
        assert step(hosted, "Download frozen source")["with"]["name"] == "${{ needs.resolve.outputs.artifact }}-source"
        assert step(hosted, "Download pinned builder")["with"]["name"] == "${{ needs.resolve.outputs.artifact }}-builder"
        assert "sha256sum --check" in step(hosted, "Load pinned builder")["run"]
        assert "LOCAL_BUILDER_IMAGE_ID" in step(hosted, "Hosted fallback")["env"]


def test_result_extraction_has_one_shared_implementation():
    helper = ROOT / "scripts/ci/result_archive.py"
    assert helper.exists(), "missing shared bounded result extractor"
    assert "TarInfo.frombuf" in helper.read_text()
    for doc in [controller(), workflow("mac-ci-pilot.yml")]:
        text = json.dumps(doc)
        assert "TarInfo.frombuf" not in text
        for job in doc["jobs"].values():
            for action in job["steps"]:
                if action.get("name") == "Extract bounded result":
                    assert "result_archive.py" in action["run"]


@pytest.mark.parametrize("mutation", [None, "checksum", "identity", "platform"])
def test_private_builder_export_and_credential_free_import(tmp_path, mutation):
    jobs = controller()["jobs"]
    fetch = step(jobs["fetch-builder"], "Export private builder")["run"]
    restore = step(jobs["hosted"], "Load pinned builder")["run"]
    executable = tmp_path / "docker"
    executable.write_text('''#!/usr/bin/env python3
import json,os,sys
args=sys.argv[1:]
with open(os.environ["OPS"],"a") as output: output.write(json.dumps(args)+"\\n")
if args[0]=="login":
    assert os.environ.get("GH_TOKEN")=="fake"
    sys.stdin.read()
elif args[0]=="save": sys.stdout.buffer.write(b"fake-image-archive")
elif args[:2]==["image","inspect"]:
    print("sha256:"+"7"*64+" "+os.environ.get("ARCH","amd64")+" linux")
elif args[0]=="load":
    assert "GH_TOKEN" not in os.environ and "DOCKER_CONFIG" not in os.environ
    assert sys.stdin.buffer.read()==b"fake-image-archive"
elif args[0]=="tag":
    assert args[1]=="sha256:"+"7"*64 and args[2]=="letovo-ci/backend-builder:123-2"
elif args[0]!="pull": sys.exit(10)
''')
    executable.chmod(0o755)
    env = {**os.environ, "PATH": f"{tmp_path}:{os.environ['PATH']}", "RUNNER_TEMP": str(tmp_path), "OPS": str(tmp_path / "ops"), "GITHUB_OUTPUT": str(tmp_path / "outputs"), "GITHUB_ACTOR": "test", "GITHUB_RUN_ID": "123", "GITHUB_RUN_ATTEMPT": "2"}
    env.pop("GH_TOKEN", None)
    env.pop("DOCKER_CONFIG", None)
    result = subprocess.run(["bash", "-e", "-c", fetch], env={**env, "GH_TOKEN": "fake", "BUILDER_IMAGE": "ghcr.io/letovo-dev/letovo-backend-builder@sha256:" + "b" * 64}, capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    assert not list(tmp_path.glob("docker-auth.*"))
    outputs = dict(line.split("=", 1) for line in (tmp_path / "outputs").read_text().splitlines())
    assert outputs["archive_sha256"] == hashlib.sha256(b"fake-image-archive").hexdigest()
    destination = tmp_path / "builder-download"
    destination.mkdir()
    (tmp_path / "builder.tar").rename(destination / "builder.tar")
    (tmp_path / "ops").write_text("")
    env |= {"BUILDER_SHA256": outputs["archive_sha256"], "BUILDER_ID": outputs["image_id"]}
    if mutation == "checksum":
        (destination / "builder.tar").write_bytes(b"tampered")
    elif mutation == "identity":
        env["BUILDER_ID"] = "sha256:" + "8" * 64
    elif mutation == "platform":
        env["ARCH"] = "arm64"
    result = subprocess.run(["bash", "-e", "-c", restore], env=env, capture_output=True, text=True)
    calls = [json.loads(line) for line in (tmp_path / "ops").read_text().splitlines()]
    if mutation:
        assert result.returncode != 0
        assert not any(call[0] == "tag" for call in calls)
        if mutation == "checksum":
            assert calls == []
    else:
        assert result.returncode == 0, result.stderr
        assert [call[0] for call in calls] == ["load", "image", "tag"]


def test_publisher_authentication_happens_after_bounded_load_and_inspection():
    steps = controller()["jobs"]["publish"]["steps"]
    load = next(i for i, s in enumerate(steps) if s["name"] == "Load bounded verified images")
    auth = next(i for i, s in enumerate(steps) if "docker login" in s.get("run", ""))
    assert load < auth
    assert "--load-only" in steps[load]["run"]
    assert "--publish-only" in steps[auth]["run"]
    assert "GH_TOKEN" not in json.dumps(steps[:auth])


def test_deploy_migrations_come_only_from_trusted_control_sha():
    job = controller()["jobs"]["deploy"]
    checkouts = [s["with"] for s in job["steps"] if s.get("uses", "").startswith("actions/checkout@")]
    assert len(checkouts) == 1
    assert checkouts[0]["path"] == "control"
    assert checkouts[0]["ref"] == "${{ needs.resolve.outputs.control_sha }}"
    assert not any(s["name"] in {"Checkout frozen migration data", "Validate migration data paths"} for s in job["steps"])
    script = step(job, "Deploy PR candidate images to live e2e")["run"]
    copies = [line for line in script.splitlines() if line.lstrip().startswith("scp ")]
    assert len(copies) == 6
    assert all(" control/docs/" in line and line.rstrip(" \\").endswith("_migration.sql") for line in copies)
    assert "candidate/docs/" not in script


def test_all_deploy_connections_require_pinned_host_keys():
    job = controller()["jobs"]["deploy"]
    assert job["env"]["LETOVO_E2E_DEPLOY_KNOWN_HOSTS"] == "${{ secrets.LETOVO_E2E_DEPLOY_KNOWN_HOSTS }}"
    assert "ssh-keyscan" not in json.dumps(job)
    validation = step(job, "Validate live e2e deployment secrets")["run"]
    assert 'test -n "$LETOVO_E2E_DEPLOY_KNOWN_HOSTS"' in validation
    connections = [line for s in job["steps"] for line in s.get("run", "").splitlines() if re.match(r"\s*(ssh|scp) ", line)]
    assert len(connections) == 9
    for line in connections:
        assert "-o StrictHostKeyChecking=yes" in line
        assert '-o UserKnownHostsFile="$RUNNER_TEMP/live-e2e-known-hosts"' in line


def test_deploy_known_hosts_is_written_with_private_permissions(tmp_path):
    job = controller()["jobs"]["deploy"]
    script = step(job, "Configure deployment SSH")["run"]
    script = script.replace("~/.ssh", str(tmp_path / "ssh"))
    keyscan = tmp_path / "ssh-keyscan"
    keyscan.write_text("#!/bin/sh\nexit 0\n")
    keyscan.chmod(0o755)
    known_hosts = "test-host ssh-ed25519 AAAATEST\n"
    result = subprocess.run(["bash", "-e", "-c", script], env={**os.environ, "PATH": f"{tmp_path}:{os.environ['PATH']}", "RUNNER_TEMP": str(tmp_path), "LETOVO_E2E_DEPLOY_HOST": "test-host", "LETOVO_E2E_DEPLOY_SSH_KEY": "test-key", "LETOVO_E2E_DEPLOY_KNOWN_HOSTS": known_hosts}, capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    path = tmp_path / "live-e2e-known-hosts"
    assert path.exists(), "missing pinned known_hosts file"
    assert path.read_text() == known_hosts + "\n"
    assert path.stat().st_mode & 0o777 == 0o600


def test_deploy_fails_closed_when_known_hosts_secret_is_absent():
    script = step(controller()["jobs"]["deploy"], "Validate live e2e deployment secrets")["run"]
    env = {**os.environ, "LETOVO_E2E_DEPLOY_USER": "test", "LETOVO_E2E_DEPLOY_SSH_KEY": "test-key", "LETOVO_E2E_DEPLOY_KNOWN_HOSTS": "", **{f"{name}_CANDIDATE_IMAGE": "test-image" for name in ("BACKEND", "REGISTRATION", "FRONTEND", "UPLOADER")}}
    result = subprocess.run(["bash", "-e", "-c", script], env=env, capture_output=True, text=True)
    assert result.returncode != 0
    assert "LETOVO_E2E_DEPLOY_KNOWN_HOSTS secret is required" in result.stdout
