#!/usr/bin/env python3

import hashlib
import importlib.util
import pathlib
import subprocess
import sys
import tempfile
import types
import unittest
from unittest import mock


sys.dont_write_bytecode = True
ROOT = pathlib.Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "scripts" / "rig" / "macos" / "loka_macos_rig.py"
SPEC = importlib.util.spec_from_file_location("loka_macos_rig", MODULE_PATH)
assert SPEC and SPEC.loader
rig = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = rig
SPEC.loader.exec_module(rig)


class DescriptorSectionTest(unittest.TestCase):
    """A descriptor is read by more than one consumer.

    The adapter reads [rig]; the scenario rail's capture guard reads
    [capture]. A descriptor carrying both must fail on what is actually wrong
    with it for this adapter -- not on the fact that a second consumer exists.
    """

    RIGS = ROOT / "scripts" / "rig" / "macos" / "rigs"

    def test_mavericks_still_loads(self):
        self.assertEqual(rig.load_descriptor(self.RIGS / "mavericks-10.9.ini").rig_id, "mavericks-10.9")

    def test_tahoe_is_refused_for_its_build_profile_not_its_shape(self):
        with self.assertRaises(rig.RigError) as caught:
            rig.load_descriptor(self.RIGS / "tahoe.ini")
        message = str(caught.exception)
        self.assertIn("build_profile macos-debug is not one this adapter runs", message)
        self.assertNotIn("must contain only", message)
        self.assertNotIn("unexpected", message)

    def test_a_stray_section_is_still_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            descriptor = pathlib.Path(directory) / "strays.ini"
            descriptor.write_text(
                (self.RIGS / "mavericks-10.9.ini").read_text(encoding="utf-8") + "\n[rigg]\nkey = value\n",
                encoding="utf-8",
            )
            with self.assertRaises(rig.RigError) as caught:
                rig.load_descriptor(descriptor)
            self.assertIn("declares unexpected [rigg]", str(caught.exception))

    def test_a_descriptor_without_its_own_section_is_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            descriptor = pathlib.Path(directory) / "capture-only.ini"
            descriptor.write_text("[capture]\nappearance = light\n", encoding="utf-8")
            with self.assertRaises(rig.RigError) as caught:
                rig.load_descriptor(descriptor)
            self.assertIn("must declare [rig]", str(caught.exception))


class MacOSRigProtocolTest(unittest.TestCase):
    def test_tracked_descriptor_separates_machine_local_mapping(self):
        descriptor = rig.load_descriptor(ROOT / "scripts" / "rig" / "macos" / "rigs" / "mavericks-10.9.ini")
        self.assertEqual(descriptor.rig_id, "mavericks-10.9")
        self.assertEqual(descriptor.supported_modes, frozenset(("flow", "inspect")))
        self.assertFalse(descriptor.disposable_for_input)

        with tempfile.TemporaryDirectory() as directory:
            local_path = pathlib.Path(directory) / "local.ini"
            local_path.write_text(
                "[local]\n"
                "vm_host_ssh = host-alias\n"
                "vm_name = VM Name\n"
                "target_host = auto\n"
                "target_user = test-user\n"
                f"target_identity_file = {directory}/id_rsa\n"
                "target_proxy_ssh = host-alias\n"
                "target_legacy_rsa = true\n"
                "target_root = /Users/test/loka-rig\n"
                f"archive_root = {directory}/archive\n",
                encoding="utf-8",
            )
            mapping = rig.load_local_mapping(local_path)
            self.assertEqual(mapping.vm_name, "VM Name")
            self.assertEqual(str(mapping.target_root), "/Users/test/loka-rig")

    def test_unknown_descriptor_vocabulary_fails_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "rig.ini"
            source = ROOT / "scripts" / "rig" / "macos" / "rigs" / "mavericks-10.9.ini"
            lines = source.read_text(encoding="utf-8").splitlines(keepends=True)
            # The stray key has to land in [rig]; appending it to the file put
            # it wherever the last section happened to be, so this stopped
            # testing anything the day the descriptor grew a [capture] section
            # after it. load_descriptor polices the [rig] vocabulary, and a
            # stray [capture] field fails closed later, at compare time.
            after_header = lines.index("[rig]\n") + 1
            mutated = lines[:after_header] + ["unexpected = value\n"] + lines[after_header:]
            path.write_text("".join(mutated), encoding="utf-8")
            with self.assertRaises(rig.RigError) as caught:
                rig.load_descriptor(path)
            self.assertEqual(caught.exception.stage, "configuration")

    def test_vm_cleanup_restores_only_a_state_the_orchestrator_changed(self):
        self.assertEqual(rig.VmLease("running").success_action(), "leave-running")
        self.assertEqual(rig.VmLease("stopped").success_action(), "stop")
        self.assertEqual(rig.VmLease("suspended").success_action(), "suspend")

    def test_cleanup_is_a_success_commit_point(self):
        self.assertFalse(rig.cleanup_allowed("failed", True, True))
        self.assertFalse(rig.cleanup_allowed("passed", False, True))
        self.assertFalse(rig.cleanup_allowed("passed", True, False))
        self.assertTrue(rig.cleanup_allowed("passed", True, True))

    def test_target_retention_reports_observed_lifecycle_state(self):
        self.assertEqual(rig.target_retained_value("not-created"), "not-created")
        self.assertEqual(rig.target_retained_value("retained"), "1")
        self.assertEqual(rig.target_retained_value("removed"), "0")
        with self.assertRaises(rig.RigError) as caught:
            rig.target_retained_value("unknown")
        self.assertEqual(caught.exception.stage, "manifest")

    def test_parallels_address_discovery_declines_missing_fact(self):
        self.assertEqual(rig.parse_parallels_ipv4("IP Addresses: 192.0.2.17\n"), "192.0.2.17")
        self.assertIsNone(rig.parse_parallels_ipv4("IP Addresses: -\n"))
        self.assertEqual(rig.parse_parallels_state("State: running\n"), "running")
        self.assertIsNone(rig.parse_parallels_state("Status unavailable\n"))

    def test_manifest_hashes_finalized_artifacts_in_stable_order(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            a_bytes = b"a\n"
            z_bytes = b"z\n"
            (root / "z.log").write_bytes(z_bytes)
            (root / "a.snap").write_bytes(a_bytes)
            hashes = rig.artifact_hashes(root)
            manifest = rig.render_manifest(
                (("rig_id", "mavericks-10.9"), ("result", "passed")),
                hashes,
            )
            expected = (
                "manifest_version=1\n"
                "rig_id=mavericks-10.9\n"
                "result=passed\n"
                f"artifact_sha256={hashlib.sha256(a_bytes).hexdigest()}  a.snap\n"
                f"artifact_sha256={hashlib.sha256(z_bytes).hexdigest()}  z.log\n"
            )
            self.assertEqual(manifest, expected)

    def test_remote_environment_precedes_exec(self):
        command = rig.remote_in_directory(
            pathlib.PurePosixPath("/tmp/run path"),
            ("/bin/echo", "hello world"),
            {"MODE": "inspect"},
        )
        self.assertEqual(command, "cd '/tmp/run path' && MODE=inspect exec /bin/echo 'hello world'")

    def test_build_publishes_the_stamp_in_the_runner_build_directory(self):
        run = object.__new__(rig.MacOSRigRun)
        run.target_source = pathlib.PurePosixPath("/target/source")
        run._run_logged_ssh = mock.Mock()
        run._build()
        configure, build = [call.args[0] for call in run._run_logged_ssh.call_args_list[:2]]
        self.assertIn("/target/source/build/macos/Debug", configure)
        self.assertIn("/target/source/build/macos/Debug", build)
        self.assertIn("--target LokaMacOSScenarioSourceStamp", build)

    def test_runner_command_names_the_scrapbook_example(self):
        run = object.__new__(rig.MacOSRigRun)
        run.target_source = pathlib.PurePosixPath("/target/source")
        run.target_artifacts = pathlib.PurePosixPath("/target/artifacts")
        run.mode = "flow"
        run.scenario = "flip-forward-back"
        run.mapping = types.SimpleNamespace(target_python="/target/python3")
        command = run._runner_command()
        self.assertIn("/target/source/build/macos/Debug/apple/macos/LokaScrapbookScenarioMacOS.app", command)
        self.assertIn(
            "exec /bin/bash tests/macos/run-scenario.sh scrapbook flip-forward-back --ci-structural",
            command,
        )

    def test_remote_marker_query_distinguishes_absence_from_transport_failure(self):
        run = object.__new__(rig.MacOSRigRun)
        run._target_ssh_args = lambda: ("ssh", "target")
        marker = pathlib.PurePosixPath("/tmp/ready")
        with mock.patch.object(rig.subprocess, "run") as query:
            query.return_value = subprocess.CompletedProcess(("ssh",), 0)
            self.assertTrue(run._remote_file_exists(marker))
            query.return_value = subprocess.CompletedProcess(("ssh",), 1)
            self.assertFalse(run._remote_file_exists(marker))
            query.return_value = subprocess.CompletedProcess(("ssh",), 255)
            self.assertIsNone(run._remote_file_exists(marker))
            query.side_effect = subprocess.TimeoutExpired(("ssh",), 15)
            self.assertIsNone(run._remote_file_exists(marker))

    def test_transport_retry_is_bounded_and_can_recover(self):
        run = object.__new__(rig.MacOSRigRun)
        run.command_log = None
        attempts = []

        def action():
            attempts.append(len(attempts) + 1)
            if len(attempts) < 3:
                raise rig.RigError("transfer", "transient failure")

        with mock.patch.object(rig.time, "sleep"):
            run._retry("transfer", "test operation", action)
        self.assertEqual(attempts, [1, 2, 3])

    def test_work_directory_validation_resolves_parent_segments_and_symlinks(self):
        tool = ROOT / "tests" / "macos" / "validate-work-dir.py"
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            build = root / "build"
            outside = root / "outside"
            build.mkdir()
            outside.mkdir()
            accepted = subprocess.run(
                (sys.executable, str(tool), str(build), str(build / "run")),
                check=False,
                text=True,
                stdout=subprocess.PIPE,
            )
            escaped = subprocess.run(
                (sys.executable, str(tool), str(build), str(build / ".." / "outside")),
                check=False,
            )
            (build / "link").symlink_to(outside, target_is_directory=True)
            linked = subprocess.run(
                (sys.executable, str(tool), str(build), str(build / "link" / "victim")),
                check=False,
            )
            self.assertEqual(accepted.returncode, 0)
            self.assertEqual(pathlib.Path(accepted.stdout.strip()), build / "run")
            self.assertNotEqual(escaped.returncode, 0)
            self.assertNotEqual(linked.returncode, 0)

    def test_vm_start_polls_until_parallels_reports_the_guest_address(self):
        run = object.__new__(rig.MacOSRigRun)
        run.mapping = types.SimpleNamespace(
            vm_host_ssh="host",
            vm_name="VM",
        )
        run.command_log = mock.Mock()
        run._vm_state = mock.Mock(return_value="stopped")
        run._ssh_alias = mock.Mock()
        run._discover_target = mock.Mock(side_effect=(False, False, True))
        run._target_ssh_args = mock.Mock(return_value=("ssh", "target"))
        with mock.patch.object(rig.time, "monotonic", return_value=0), mock.patch.object(
            rig.time, "sleep"
        ), mock.patch.object(rig.subprocess, "run") as target_query:
            target_query.return_value = subprocess.CompletedProcess(("ssh",), 0)
            run._prepare_vm()
        self.assertEqual(run._discover_target.call_count, 3)
        run._ssh_alias.assert_called_once_with(
            "host",
            ("/usr/local/bin/prlctl", "start", "VM"),
            "vm-start",
        )

    def test_cleanup_keeps_checkout_until_remote_cleanup_succeeds(self):
        run = object.__new__(rig.MacOSRigRun)
        run.target_run_root = pathlib.PurePosixPath("/target/run")
        run.target_state = "retained"
        run.checkout = pathlib.Path("/local/checkout")
        run.repo = pathlib.Path("/local/repo")
        run.vm_lease = rig.VmLease("stopped")
        run.mapping = types.SimpleNamespace(vm_host_ssh="host", vm_name="VM")
        operations = []
        run._target_ssh = mock.Mock(side_effect=lambda *args, **kwargs: operations.append("target"))
        run._ssh_alias = mock.Mock(side_effect=lambda *args, **kwargs: operations.append("vm"))
        run._run = mock.Mock(side_effect=lambda *args, **kwargs: operations.append("checkout"))
        run._cleanup_success()
        self.assertEqual(operations, ["target", "vm", "checkout"])
        self.assertEqual(run.target_state, "removed")

        run.target_state = "retained"
        run._target_ssh.reset_mock(side_effect=True)
        run._target_ssh.side_effect = lambda *args, **kwargs: None
        run._ssh_alias.reset_mock(side_effect=True)
        run._ssh_alias.side_effect = rig.RigError("cleanup", "restore failed")
        run._run.reset_mock()
        with self.assertRaises(rig.RigError):
            run._cleanup_success()
        run._run.assert_not_called()


if __name__ == "__main__":
    unittest.main()
