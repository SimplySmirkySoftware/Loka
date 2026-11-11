#!/usr/bin/env python3
"""Run one specified Loka commit through the macOS Parallels rig adapter.

Machine-specific names and paths come only from a local mapping.
"""

from __future__ import annotations

import argparse
import dataclasses
import os
import pathlib
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import time
from typing import Callable, Optional, Sequence, TypeVar


sys.dont_write_bytecode = True
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from loka_rig_common import (
    REPOSITORY_ROOT,
    RIG_ID_PATTERN,
    SUPPORTED_ARTIFACT_CONTRACT_VERSION,
    SUPPORTED_DESCRIPTOR_VERSION,
    CommandLog,
    RigError,
    RunProgress,
    RunResult,
    artifact_hashes,
    cleanup_allowed,
    execute_adapter,
    make_run_id,
    parse_bool,
    read_declared_section,
    render_manifest,
    require_keys,
    resolve_commit,
    target_retained_value,
    utc_now,
    write_manifest,
)


SUPPORTED_MODES = frozenset(("flow", "inspect"))
RetryResult = TypeVar("RetryResult")


@dataclasses.dataclass(frozen=True)
class RigDescriptor:
    rig_id: str
    os_version: str
    os_build: str
    machine: str
    build_architecture: str
    build_profile: str
    supported_modes: frozenset[str]
    disposable_for_input: bool
    capture_adapter: str
    recording_adapter: str
    artifact_contract_version: str


@dataclasses.dataclass(frozen=True)
class LocalMapping:
    vm_host_ssh: str
    vm_name: str
    target_host: str
    target_user: str
    target_identity_file: pathlib.Path
    target_proxy_ssh: str
    target_legacy_rsa: bool
    target_root: pathlib.PurePosixPath
    archive_root: pathlib.Path
    vm_snapshot: str
    target_python: str


@dataclasses.dataclass(frozen=True)
class VmLease:
    initial_state: str

    def success_action(self) -> str:
        if self.initial_state == "stopped":
            return "stop"
        if self.initial_state == "suspended":
            return "suspend"
        return "leave-running"


def load_descriptor(path: pathlib.Path) -> RigDescriptor:
    section = read_declared_section(path, "rig", permitted=("rig", "capture"))
    expected = {
        "descriptor_version",
        "rig_id",
        "os_version",
        "os_build",
        "machine",
        "build_architecture",
        "build_profile",
        "supported_modes",
        "disposable_for_input",
        "capture_adapter",
        "recording_adapter",
        "artifact_contract_version",
    }
    require_keys(section, expected, path)
    if section["descriptor_version"] != SUPPORTED_DESCRIPTOR_VERSION:
        raise RigError("configuration", f"{path}: unsupported descriptor_version")
    if section["artifact_contract_version"] != SUPPORTED_ARTIFACT_CONTRACT_VERSION:
        raise RigError("configuration", f"{path}: unsupported artifact_contract_version")
    rig_id = section["rig_id"].strip()
    if not RIG_ID_PATTERN.fullmatch(rig_id):
        raise RigError("configuration", f"{path}: invalid rig_id")
    modes = frozenset(part.strip() for part in section["supported_modes"].split(",") if part.strip())
    if not modes or not modes.issubset(SUPPORTED_MODES):
        raise RigError("configuration", f"{path}: unsupported or empty supported_modes")
    if section["build_profile"] != "macos-10.6-sdk-i386":
        raise RigError(
            "configuration",
            f"{path}: build_profile {section['build_profile'].strip()} is not one this adapter runs "
            "(macos-10.6-sdk-i386); a descriptor can declare a capture environment for the "
            "scenario rail without being runnable here",
        )
    return RigDescriptor(
        rig_id=rig_id,
        os_version=section["os_version"].strip(),
        os_build=section["os_build"].strip(),
        machine=section["machine"].strip(),
        build_architecture=section["build_architecture"].strip(),
        build_profile=section["build_profile"].strip(),
        supported_modes=modes,
        disposable_for_input=parse_bool(section["disposable_for_input"], "disposable_for_input", path),
        capture_adapter=section["capture_adapter"].strip(),
        recording_adapter=section["recording_adapter"].strip(),
        artifact_contract_version=section["artifact_contract_version"].strip(),
    )


def load_local_mapping(path: pathlib.Path) -> LocalMapping:
    section = read_declared_section(path, "local")
    required = {
        "vm_host_ssh",
        "vm_name",
        "target_host",
        "target_user",
        "target_identity_file",
        "target_proxy_ssh",
        "target_legacy_rsa",
        "target_root",
        "archive_root",
    }
    optional = {"vm_snapshot", "target_python"}
    actual = set(section.keys())
    missing = sorted(required - actual)
    unknown = sorted(actual - required - optional)
    if missing or unknown:
        detail = []
        if missing:
            detail.append("missing " + ", ".join(missing))
        if unknown:
            detail.append("unknown " + ", ".join(unknown))
        raise RigError("configuration", f"{path}: {'; '.join(detail)}")
    target_root = pathlib.PurePosixPath(section["target_root"].strip())
    archive_root = pathlib.Path(section["archive_root"].strip()).expanduser()
    if not target_root.is_absolute() or str(target_root) == "/":
        raise RigError("configuration", f"{path}: target_root must be a narrow absolute path")
    if not archive_root.is_absolute() or str(archive_root) == "/":
        raise RigError("configuration", f"{path}: archive_root must be a narrow absolute path")
    identity_file = pathlib.Path(section["target_identity_file"].strip()).expanduser()
    if not identity_file.is_absolute() or str(identity_file) == "/":
        raise RigError("configuration", f"{path}: target_identity_file must be a narrow absolute path")
    for field in ("vm_host_ssh", "vm_name", "target_host", "target_user"):
        if not section[field].strip() or "\n" in section[field] or "\r" in section[field]:
            raise RigError("configuration", f"{path}: invalid {field}")
    return LocalMapping(
        vm_host_ssh=section["vm_host_ssh"].strip(),
        vm_name=section["vm_name"].strip(),
        target_host=section["target_host"].strip(),
        target_user=section["target_user"].strip(),
        target_identity_file=identity_file,
        target_proxy_ssh=section["target_proxy_ssh"].strip(),
        target_legacy_rsa=parse_bool(section["target_legacy_rsa"], "target_legacy_rsa", path),
        target_root=target_root,
        archive_root=archive_root,
        vm_snapshot=section.get("vm_snapshot", "").strip(),
        target_python=section.get("target_python", "/usr/bin/python3").strip(),
    )


def remote_command(arguments: Sequence[str]) -> str:
    return " ".join(shlex.quote(argument) for argument in arguments)


def remote_in_directory(directory: pathlib.PurePosixPath, arguments: Sequence[str], environment: dict[str, str] | None = None) -> str:
    assignments = []
    if environment:
        assignments = [f"{key}={shlex.quote(value)}" for key, value in sorted(environment.items())]
    environment_prefix = " ".join(assignments)
    if environment_prefix:
        environment_prefix += " "
    command = remote_command(tuple(arguments))
    return f"cd {shlex.quote(str(directory))} && {environment_prefix}exec {command}"


def parse_parallels_ipv4(output: str) -> Optional[str]:
    match = re.search(r"IP Addresses:\s+([0-9]+(?:\.[0-9]+){3})", output)
    return match.group(1) if match else None


def parse_parallels_state(output: str) -> Optional[str]:
    match = re.search(r"^State:\s+(running|suspended|stopped)\s*$", output, re.MULTILINE | re.IGNORECASE)
    return match.group(1).lower() if match else None


class MacOSRigRun:
    def __init__(
        self,
        repo: pathlib.Path,
        descriptor: RigDescriptor,
        mapping: LocalMapping,
        requested_ref: str,
        mode: str,
        scenario: str,
        release_after_ready: bool,
    ):
        self.repo = repo
        self.descriptor = descriptor
        self.mapping = mapping
        self.requested_ref = requested_ref
        self.mode = mode
        self.scenario = scenario
        self.release_after_ready = release_after_ready
        self.commit_sha = ""
        self.run_id = ""
        self.archive: Optional[pathlib.Path] = None
        self.checkout: Optional[pathlib.Path] = None
        self.target_run_root = pathlib.PurePosixPath("/")
        self.target_source = pathlib.PurePosixPath("/")
        self.target_artifacts = pathlib.PurePosixPath("/")
        self.target_destination = ""
        self.target_state = "not-created"
        self.vm_lease: Optional[VmLease] = None
        self.progress = RunProgress()
        self.command_log: Optional[CommandLog] = None

    def _run(self, args: Sequence[str], stage: str, **kwargs: object) -> subprocess.CompletedProcess[str]:
        try:
            return subprocess.run(args, check=True, text=True, **kwargs)
        except subprocess.CalledProcessError as error:
            raise RigError(stage, f"{args[0]} command exited {error.returncode}") from error
        except subprocess.TimeoutExpired as error:
            raise RigError(stage, f"{args[0]} command timed out") from error
        except OSError as error:
            raise RigError(stage, f"could not execute {args[0]}: {error}") from error

    def _ssh_alias(
        self, alias: str, arguments: Sequence[str], stage: str, capture: bool = True
    ) -> subprocess.CompletedProcess[str]:
        kwargs: dict[str, object] = {"timeout": 30}
        if capture:
            kwargs["stdout"] = subprocess.PIPE
            kwargs["stderr"] = subprocess.PIPE
        return self._run(
            ("ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=10", alias, remote_command(arguments)),
            stage,
            **kwargs,
        )

    def _discover_target(self) -> bool:
        host = self.mapping.target_host
        if host == "auto":
            result = self._ssh_alias(
                self.mapping.vm_host_ssh,
                ("/usr/local/bin/prlctl", "list", "-i", self.mapping.vm_name),
                "target-discovery",
            )
            discovered = parse_parallels_ipv4(result.stdout or "")
            if not discovered:
                return False
            host = discovered
        self.target_destination = f"{self.mapping.target_user}@{host}"
        return True

    def _target_transport_options(self, timeout: str) -> list[str]:
        options = [
            "-o",
            "BatchMode=yes",
            "-o",
            f"ConnectTimeout={timeout}",
            "-o",
            "StrictHostKeyChecking=accept-new",
        ]
        if self.mapping.target_legacy_rsa:
            options.extend(("-o", "HostKeyAlgorithms=+ssh-rsa", "-o", "PubkeyAcceptedAlgorithms=+ssh-rsa"))
        options.extend(("-o", "IdentitiesOnly=yes", "-i", str(self.mapping.target_identity_file)))
        if self.mapping.target_proxy_ssh:
            options.extend(("-J", self.mapping.target_proxy_ssh))
        return options

    def _target_ssh_args(self, timeout: str = "10") -> tuple[str, ...]:
        if not self.target_destination:
            raise RigError("target-preflight", "target endpoint was not discovered")
        return tuple(("ssh",) + tuple(self._target_transport_options(timeout)) + (self.target_destination,))

    def _target_remote_shell(self) -> str:
        return remote_command(("ssh",) + tuple(self._target_transport_options("10")))

    def _target_ssh(
        self, arguments: Sequence[str], stage: str, capture: bool = True
    ) -> subprocess.CompletedProcess[str]:
        kwargs: dict[str, object] = {"timeout": 30}
        if capture:
            kwargs["stdout"] = subprocess.PIPE
            kwargs["stderr"] = subprocess.PIPE
        return self._run(self._target_ssh_args() + (remote_command(arguments),), stage, **kwargs)

    def _retry(
        self, stage: str, operation: str, action: Callable[[], RetryResult]
    ) -> RetryResult:
        last_error: Optional[RigError] = None
        for attempt in range(1, 4):
            try:
                return action()
            except RigError as error:
                last_error = error
                if self.command_log:
                    self.command_log.write(f"{stage} {operation} attempt {attempt}/3 failed")
                if attempt < 3:
                    time.sleep(2)
        if last_error is None:
            raise RigError(stage, f"{operation} retry did not execute")
        raise last_error

    def _rsync(self, arguments: Sequence[str], stage: str) -> None:
        self._retry(
            stage,
            "rsync",
            lambda: self._run(("rsync", "--partial", "--timeout=30") + tuple(arguments), stage),
        )

    def _target_query(
        self, arguments: Sequence[str], stage: str
    ) -> subprocess.CompletedProcess[str]:
        return self._retry(
            stage,
            "read-only query",
            lambda: self._target_ssh(arguments, stage),
        )

    def _vm_state(self) -> str:
        result = self._ssh_alias(
            self.mapping.vm_host_ssh,
            ("/usr/local/bin/prlctl", "list", "-i", self.mapping.vm_name),
            "vm-preflight",
        )
        state = parse_parallels_state(result.stdout or "")
        if not state:
            raise RigError("vm-preflight", "could not determine VM state")
        return state

    def _prepare_vm(self) -> None:
        initial = self._vm_state()
        self.vm_lease = VmLease(initial)
        self.command_log.write(f"VM initial state: {initial}")
        if initial in ("stopped", "suspended"):
            self._ssh_alias(
                self.mapping.vm_host_ssh,
                ("/usr/local/bin/prlctl", "start", self.mapping.vm_name),
                "vm-start",
            )
        deadline = time.monotonic() + 120
        while True:
            discovered = self._discover_target()
            if discovered:
                attempt = subprocess.run(
                    self._target_ssh_args("5") + ("true",),
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                )
                if attempt.returncode == 0:
                    return
            if time.monotonic() >= deadline:
                if not discovered:
                    raise RigError("target-discovery", "Parallels did not report a target IPv4 address")
                raise RigError("target-preflight", "target SSH did not become ready")
            time.sleep(2)

    def _preflight_target(self) -> None:
        product = self._target_query(("/usr/bin/sw_vers", "-productVersion"), "target-preflight")
        build = self._target_query(("/usr/bin/sw_vers", "-buildVersion"), "target-preflight")
        machine = self._target_query(("/usr/bin/uname", "-m"), "target-preflight")
        facts = (product.stdout.strip(), build.stdout.strip(), machine.stdout.strip())
        expected = (self.descriptor.os_version, self.descriptor.os_build, self.descriptor.machine)
        if facts != expected:
            raise RigError("target-preflight", f"descriptor facts {expected} do not match target {facts}")
        console = self._target_query(
            ("/usr/bin/stat", "-f", "%Su", "/dev/console"),
            "aqua-preflight",
        ).stdout.strip()
        if console in ("", "root", "loginwindow"):
            raise RigError("aqua-preflight", "no active Aqua console user")
        self._target_query(("/usr/bin/pgrep", "-x", "WindowServer"), "aqua-preflight")
        required = (
            "/opt/local/bin/cmake",
            "/opt/local/bin/ninja",
            self.mapping.target_python,
            "/usr/sbin/screencapture",
            "/Developer/SDKs/MacOSX10.6.sdk",
        )
        for path in required:
            self._target_query(("/bin/test", "-e", path), "toolchain-preflight")

    def _prepare_checkout(self) -> None:
        self.commit_sha = resolve_commit(self.repo, self.requested_ref)
        self.run_id = make_run_id(self.commit_sha, self.mode, self.scenario)
        self.archive = self.mapping.archive_root / self.descriptor.rig_id / self.run_id
        self.checkout = self.repo / "build" / "loka-rig" / "checkouts" / self.run_id
        self.target_run_root = self.mapping.target_root / self.run_id
        self.target_source = self.target_run_root / "source"
        self.target_artifacts = self.target_source / "build" / "loka-rig-run"
        self.archive.mkdir(parents=True, exist_ok=False)
        self.command_log = CommandLog(self.archive / "orchestrator.log")
        self.command_log.write(f"Resolved {self.requested_ref} to {self.commit_sha}")
        self.checkout.parent.mkdir(parents=True, exist_ok=True)
        self._run(
            ("git", "-C", str(self.repo), "worktree", "add", "--detach", str(self.checkout), self.commit_sha),
            "checkout",
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

    def _transfer_source(self) -> None:
        self._target_ssh(
            ("/bin/mkdir", "-p", str(self.target_source)),
            "source-transfer",
        )
        self.target_state = "retained"
        self._rsync(
            (
                "--archive",
                "--delete",
                "-e",
                self._target_remote_shell(),
                "--exclude=.git",
                "--exclude=build",
                f"{self.checkout}/",
                f"{self.target_destination}:{self.target_source}/",
            ),
            "source-transfer",
        )

    def _run_logged_ssh(self, command: str, log_name: str, stage: str) -> None:
        assert self.archive is not None
        log_path = self.archive / log_name
        marker = self.target_run_root / f".{stage}-passed"
        wrapped = f"/bin/sh -c {shlex.quote(command)} && /usr/bin/touch {shlex.quote(str(marker))}"
        with log_path.open("ab") as output:
            process = subprocess.Popen(
                self._target_ssh_args() + (wrapped,),
                stdout=output,
                stderr=subprocess.STDOUT,
            )
            try:
                deadline = time.monotonic() + 1800
                while self._remote_file_exists(marker) is not True:
                    result = process.poll()
                    if result is not None:
                        time.sleep(1)
                        if self._remote_file_exists(marker) is True:
                            break
                        raise RigError(stage, f"remote command exited {result}; see {log_path}")
                    if time.monotonic() >= deadline:
                        observed = self._remote_file_exists(marker)
                        if observed is True:
                            break
                        if observed is None:
                            raise RigError(stage, f"target marker query unavailable; see {log_path}")
                        raise RigError(stage, f"remote command timed out; see {log_path}")
                    time.sleep(2)
            finally:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()

    def _build(self) -> None:
        build_dir = self.target_source / "build" / "macos" / "Debug"
        configure = remote_in_directory(
            self.target_source,
            (
                "/opt/local/bin/cmake",
                "-S",
                ".",
                "-B",
                str(build_dir),
                "-G",
                "Ninja",
                "-DTEST_BUILD=ON",
                "-DLOKA_WARNINGS_AS_ERRORS=ON",
                "-DCMAKE_BUILD_TYPE=Debug",
                "-DCMAKE_MAKE_PROGRAM=/opt/local/bin/ninja",
                "-DCMAKE_OSX_SYSROOT=/Developer/SDKs/MacOSX10.6.sdk",
                "-DCMAKE_OSX_DEPLOYMENT_TARGET=10.6",
                "-DCMAKE_OSX_ARCHITECTURES=i386",
            ),
            {"PATH": "/opt/local/bin:/usr/bin:/bin:/usr/sbin:/sbin"},
        )
        build = remote_in_directory(
            self.target_source,
            (
                "/opt/local/bin/cmake",
                "--build",
                str(build_dir),
                "--target",
                "LokaMacOSScenarioSourceStamp",
                "--parallel",
                "2",
            ),
            {"PATH": "/opt/local/bin:/usr/bin:/bin:/usr/sbin:/sbin"},
        )
        # The scenario runner stages Scrapbook packages through the host lrpc
        # tool; a fresh checkout must build it or the runner refuses. The
        # target build above cannot supply it: lrpc is a host tool, built by
        # its own configure into build/host/lrpc, and this rail cross-builds
        # the app for i386 against the 10.6 SDK. Leave that bare, the way the
        # runner's own remediation line spells it, so lrpc is compiled for the
        # machine that has to run it.
        configure_lrpc = remote_in_directory(
            self.target_source,
            (
                "/opt/local/bin/cmake",
                "-S",
                "tools/lrpc",
                "-B",
                "build/host/lrpc",
            ),
            {"PATH": "/opt/local/bin:/usr/bin:/bin:/usr/sbin:/sbin"},
        )
        build_lrpc = remote_in_directory(
            self.target_source,
            (
                "/opt/local/bin/cmake",
                "--build",
                "build/host/lrpc",
                "--parallel",
                "2",
            ),
            {"PATH": "/opt/local/bin:/usr/bin:/bin:/usr/sbin:/sbin"},
        )
        self._run_logged_ssh(configure, "build.log", "configure")
        self._run_logged_ssh(build, "build.log", "build")
        self._run_logged_ssh(configure_lrpc, "build.log", "configure-lrpc")
        self._run_logged_ssh(build_lrpc, "build.log", "build-lrpc")

    def _runner_command(self) -> str:
        app = self.target_source / "build" / "macos" / "Debug" / "apple" / "macos" / "LokaScrapbookScenarioMacOS.app"
        flag = "--inspect" if self.mode == "inspect" else "--ci-structural"
        environment = {
            "LOKA_MACOS_SCENARIO_APP": str(app),
            "LOKA_MACOS_SCENARIO_CAPTURE_DESKTOP": "1",
            "LOKA_MACOS_SCENARIO_RETAIN_ON_FAILURE": "1",
            "LOKA_MACOS_SCENARIO_WORK": str(self.target_artifacts),
            "PATH": "/opt/local/bin:/usr/bin:/bin:/usr/sbin:/sbin",
            "PYTHON3": self.mapping.target_python,
        }
        return remote_in_directory(
            self.target_source,
            ("/bin/bash", "tests/macos/run-scenario.sh", "scrapbook", self.scenario, flag),
            environment,
        )

    def _remote_file_exists(self, path: pathlib.PurePosixPath) -> Optional[bool]:
        try:
            result = subprocess.run(
                self._target_ssh_args() + (remote_command(("/bin/test", "-f", str(path))),),
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                timeout=15,
            )
        except subprocess.TimeoutExpired:
            return None
        except OSError:
            return None
        if result.returncode == 0:
            return True
        if result.returncode == 1:
            return False
        return None

    def _publish_release(self) -> None:
        with tempfile.NamedTemporaryFile("w", encoding="ascii", delete=False) as output:
            output.write("release-inspection\n")
            temporary = pathlib.Path(output.name)
        remote_temporary = pathlib.PurePosixPath(str(self.target_artifacts / "release.tmp"))
        try:
            self._retry(
                "inspect-release",
                "atomic publication",
                lambda: self._publish_release_attempt(temporary, remote_temporary),
            )
        finally:
            temporary.unlink(missing_ok=True)

    def _publish_release_attempt(
        self, temporary: pathlib.Path, remote_temporary: pathlib.PurePosixPath
    ) -> None:
        self._run(
            tuple(("scp", "-q") + tuple(self._target_transport_options("10")))
            + (str(temporary), f"{self.target_destination}:{remote_temporary}"),
            "inspect-release",
        )
        self._target_ssh(
            ("/bin/mv", str(remote_temporary), str(self.target_artifacts / "release")),
            "inspect-release",
        )

    def _launch(self) -> None:
        assert self.archive is not None
        runner_log = (self.archive / "runner-command.log").open("wb")
        process = subprocess.Popen(
            self._target_ssh_args() + (self._runner_command(),),
            stdout=runner_log,
            stderr=subprocess.STDOUT,
        )
        try:
            deadline = time.monotonic() + 140
            if self.mode == "inspect":
                ready = self.target_artifacts / "ready"
                while self._remote_file_exists(ready) is not True:
                    if process.poll() is not None:
                        raise RigError("inspect-ready", "scenario exited before publishing ready")
                    if time.monotonic() >= deadline:
                        observed = self._remote_file_exists(ready)
                        if observed is True:
                            break
                        if observed is None:
                            raise RigError("inspect-ready", "target ready-marker query unavailable")
                        raise RigError("inspect-ready", "timed out waiting for ready")
                    time.sleep(0.5)
                self.command_log.write("Inspect ready marker observed; app is held")
                if not self.release_after_ready:
                    input("Inspect the target, then press Enter to release it: ")
                self._publish_release()
                deadline = time.monotonic() + 140
            verified = self.target_artifacts / "verified"
            while self._remote_file_exists(verified) is not True:
                if process.poll() is not None:
                    observed_after_exit = False
                    for _ in range(5):
                        time.sleep(1)
                        if self._remote_file_exists(verified) is True:
                            observed_after_exit = True
                            break
                    if observed_after_exit:
                        break
                    raise RigError("runtime", "scenario command exited before runner verification")
                if time.monotonic() >= deadline:
                    observed = self._remote_file_exists(verified)
                    if observed is True:
                        break
                    if observed is None:
                        raise RigError("runtime", "target verification-marker query unavailable")
                    raise RigError("runtime", "timed out waiting for runner verification")
                time.sleep(2)
            self.command_log.write("Atomic runner verification marker observed")
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
            runner_log.close()

    def _collect(self) -> None:
        assert self.archive is not None
        self._rsync(
            (
                "--archive",
                "-e",
                self._target_remote_shell(),
                f"{self.target_destination}:{self.target_artifacts}/",
                f"{self.archive}/",
            ),
            "artifact-collection",
        )
        required = {
            "actual.png",
            "actual.profile",
            "actual.audit",
            "build.log",
            "complete",
            "desktop-after.png",
            "desktop-before.png",
            "orchestrator.log",
            "runner-command.log",
            "runner.log",
            "verified",
        }
        if self.mode == "inspect":
            required.update(("ready", "release"))
        missing = sorted(name for name in required if not (self.archive / name).is_file())
        if missing:
            raise RigError("artifact-collection", "missing artifacts: " + ", ".join(missing))

    def finalize_manifest(self, result: str) -> None:
        assert self.archive is not None
        vm_action = self.vm_lease.success_action() if self.vm_lease else "not-acquired"
        common_result = RunResult.from_progress(
            adapter="macos",
            rig_id=self.descriptor.rig_id,
            requested_ref=self.requested_ref,
            commit_sha=self.commit_sha,
            mode=self.mode,
            result=result,
            progress=self.progress,
            recording_status="not-requested",
            target_retained=target_retained_value(self.target_state),
            target_workdir=str(self.target_run_root),
            next_diagnostic_command="use configured target SSH mapping" if result != "passed" else "none",
        )
        adapter_fields = (
            ("descriptor_version", SUPPORTED_DESCRIPTOR_VERSION),
            ("artifact_contract_version", self.descriptor.artifact_contract_version),
            ("os_version", self.descriptor.os_version),
            ("os_build", self.descriptor.os_build),
            ("machine", self.descriptor.machine),
            ("build_architecture", self.descriptor.build_architecture),
            ("build_profile", self.descriptor.build_profile),
            ("vm_snapshot", self.mapping.vm_snapshot or "not-configured"),
            ("vm_initial_state", self.vm_lease.initial_state if self.vm_lease else "not-acquired"),
            ("vm_success_action", vm_action),
            ("scenario", self.scenario),
            ("capture_adapter", self.descriptor.capture_adapter),
            ("recording_adapter", self.descriptor.recording_adapter),
        )
        write_manifest(self.archive, common_result, adapter_fields)

    def _best_effort_collect(self) -> None:
        if str(self.target_artifacts) == "/" or self.archive is None:
            return
        try:
            subprocess.run(
                (
                    "rsync",
                    "--partial",
                    "--timeout=15",
                    "--archive",
                    "-e",
                    self._target_remote_shell(),
                    f"{self.target_destination}:{self.target_artifacts}/",
                    f"{self.archive}/",
                ),
                check=False,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                timeout=60,
            )
        except (OSError, subprocess.TimeoutExpired):
            pass

    def _cleanup_success(self) -> None:
        # Keep the reproducible checkout until every remote cleanup step has
        # succeeded, so a cleanup failure always leaves diagnostic source.
        self._target_ssh(
            ("/bin/rm", "-rf", str(self.target_run_root)),
            "cleanup",
        )
        self.target_state = "removed"
        if self.vm_lease:
            action = self.vm_lease.success_action()
            if action == "stop":
                self._ssh_alias(
                    self.mapping.vm_host_ssh,
                    ("/usr/local/bin/prlctl", "stop", self.mapping.vm_name),
                    "cleanup",
                )
            elif action == "suspend":
                self._ssh_alias(
                    self.mapping.vm_host_ssh,
                    ("/usr/local/bin/prlctl", "suspend", self.mapping.vm_name),
                    "cleanup",
                )
        assert self.checkout is not None
        self._run(
            ("git", "-C", str(self.repo), "worktree", "remove", "--force", str(self.checkout)),
            "cleanup",
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

    def prepare(self) -> None:
        self._prepare_checkout()
        if self.mode not in self.descriptor.supported_modes:
            raise RigError("configuration", f"{self.descriptor.rig_id} does not support {self.mode}")
        self._prepare_vm()
        self._preflight_target()
        self._transfer_source()

    def build(self) -> None:
        self._build()

    def run_runtime(self) -> None:
        self._launch()

    def collect(self) -> None:
        self._collect()

    def best_effort_collect(self) -> None:
        self._best_effort_collect()

    def note_failure(self) -> None:
        if self.command_log:
            self.command_log.write(
                f"FAILED at {self.progress.failure_stage}: {self.progress.failure_message}"
            )

    def cleanup_success(self) -> None:
        self._cleanup_success()

    def execute(self) -> pathlib.Path:
        return execute_adapter(self)


def parse_args(arguments: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    run = subparsers.add_parser("run", help="build and run one specified commit")
    run.add_argument("rig_id")
    run.add_argument("--ref", required=True)
    run.add_argument("--mode", required=True, choices=sorted(SUPPORTED_MODES))
    run.add_argument("--scenario", default="startup", choices=("startup", "flip-forward-back"))
    run.add_argument("--local-config", type=pathlib.Path)
    run.add_argument("--release-after-ready", action="store_true", help="test-only non-interactive inspect release")
    return parser.parse_args(arguments)


def run_rig(
    *,
    repo: pathlib.Path,
    rig_id: str,
    requested_ref: str,
    mode: str,
    local_config: Optional[pathlib.Path],
    scenario: str = "startup",
    release_after_ready: bool = False,
) -> pathlib.Path:
    script = pathlib.Path(__file__).resolve()
    descriptor_path = script.parent / "rigs" / f"{rig_id}.ini"
    local_path = local_config
    if local_path is None:
        configured = os.environ.get("LOKA_RIG_LOCAL_CONFIG")
        local_path = pathlib.Path(configured) if configured else pathlib.Path.home() / ".config" / "loka" / "rigs" / f"{rig_id}.ini"
    descriptor = load_descriptor(descriptor_path)
    mapping = load_local_mapping(local_path)
    run = MacOSRigRun(
        repo=repo,
        descriptor=descriptor,
        mapping=mapping,
        requested_ref=requested_ref,
        mode=mode,
        scenario=scenario,
        release_after_ready=release_after_ready,
    )
    return run.execute()


def main(arguments: Sequence[str]) -> int:
    args = parse_args(arguments)
    try:
        archive = run_rig(
            repo=REPOSITORY_ROOT,
            rig_id=args.rig_id,
            requested_ref=args.ref,
            mode=args.mode,
            local_config=args.local_config,
            scenario=args.scenario,
            release_after_ready=args.release_after_ready,
        )
    except RigError as error:
        print(f"{error.stage} stage failed: {error}", file=sys.stderr)
        return 1
    print(f"macOS rig passed: {archive}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
