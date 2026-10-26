"""Offline contract tests for the tag-only immutable GitHub release publisher."""

import contextlib
import copy
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch


SCRIPT = Path(__file__).resolve().parents[1] / ".github/scripts/publish_release.py"
SHA = "1" * 40
OLD_SHA = "2" * 40
VERSION = "0.1.0.0"
TAG = "v0.1.0.0"
REPOSITORY = "example/chunfe"
URL = f"https://github.com/{REPOSITORY}/releases/tag/{TAG}"
NAMES = ["chunfe-0.1.0.0-macos-arm64.dmg",
         "chunfe-0.1.0.0-windows-x64-setup.exe",
         "chunfe-0.1.0.0-linux-x64.flatpak"]
COMMIT = {"sha": SHA, "html_url": f"https://github.com/{REPOSITORY}/commit/{SHA}",
          "commit": {"message": "Update application libraries\n\nDetailed explanation", "author": {
              "name": "application Developer", "date": "2026-10-20T01:02:03Z", "email": "private@example.com"}}}
NOTES = (f"Chunfe version: {VERSION}\nCommit: [{SHA}]({COMMIT['html_url']})\n"
         "Subject: Update application libraries\nAuthor: application Developer\nDate: 2026-10-20T01:02:03Z\n\n"
         f"Workflow run: https://github.com/{REPOSITORY}/actions/runs/1234\n\n"
         f"<!-- chunfe-release-sha: {SHA} -->\n")


class FakeGitHub:
    """Model remote state, replacing only the external gh process boundary."""

    def __init__(self, files):
        self.files = files
        self.tag_sha = SHA
        self.release = None
        self.latest_release = None
        self.contents = {}
        self.writes = []
        self.failures = {}
        self.downloaded = []
        self.events = []
        self.stale_release_lookup = False
        self.graphql_response = None
        self.change_tag_after_upload = False
        self.change_tag_after_latest = False
        self.latest_after_upload = None
        self.corrupt_upload = False
        self.commit_response = COMMIT
        self.ref_response = None
        self.release_response = None
        self.publish_after_upload = False
        self.leave_draft = False
        self.repository_response = {"default_branch": "main"}
        self.workflows_changed_since_tag = False

    def add_asset(self, name, data, *, digest=True):
        asset_id = max(self.contents, default=0) + 1
        self.contents[asset_id] = data
        self.release["assets"].append({"id": asset_id, "name": name,
            "state": "uploaded", "size": len(data),
            "digest": "sha256:" + hashlib.sha256(data).hexdigest() if digest else None})

    def existing_release(self, *, draft=False, names=NAMES, digest=True, sha=SHA):
        self.tag_sha = sha
        self.release = {"id": 100, "tag_name": TAG, "target_commitish": "main", "draft": draft,
                        "prerelease": False, "immutable": False, "html_url": URL,
                        "name": f"Chunfe {VERSION}", "body": NOTES, "assets": []}
        for name in names:
            self.add_asset(name, self.files[name].read_bytes(), digest=digest)

    def __call__(self, command, **kwargs):
        self.events.append(command)
        assert command[0] == "gh"
        assert "test-secret" not in " ".join(command)
        assert kwargs["env"]["GH_TOKEN"] == "test-secret"
        args = command[1:]
        current_sha = os.environ["GITHUB_SHA"].lower()
        key = tuple(args[:2])
        if key in self.failures:
            code, message = self.failures[key]
            return subprocess.CompletedProcess(command, code, "", message)
        if args[0] == "api":
            endpoint = args[1]
            method = args[args.index("--method") + 1] if "--method" in args else "GET"
            fields = {args[index + 1].split("=", 1)[0]: args[index + 1].split("=", 1)[1]
                      for index, value in enumerate(args) if value in ("-f", "-F")}
            if "/git/ref" in endpoint:
                assert method == "GET", "The publisher must never create or move a tag"
            if endpoint == f"repos/{REPOSITORY}":
                assert method == "GET"
                value = self.repository_response
            elif endpoint == "graphql":
                assert fields["owner"] == "example" and fields["name"] == "chunfe" and fields["tag"] == TAG
                found = {"databaseId": 100} if self.release else None
                if self.stale_release_lookup and "create-draft" in self.writes:
                    found = None
                value = self.graphql_response if self.graphql_response is not None else {
                    "data": {"repository": {"release": found}}}
            elif endpoint.endswith(f"/commits/tags/{TAG}"):
                assert method == "GET" and args[-2:] == ["--jq", ".sha"]
                if self.tag_sha is None:
                    return subprocess.CompletedProcess(command, 1, "", "gh: No commit found (HTTP 422)\n")
                return subprocess.CompletedProcess(command, 0, self.tag_sha, "")
            elif endpoint.endswith(f"/commits/{current_sha}"):
                value = self.commit_response
            elif endpoint.endswith(f"/git/ref/tags/{TAG}"):
                if self.tag_sha is None:
                    return subprocess.CompletedProcess(command, 1, "", "gh: Not Found (HTTP 404)\n")
                value = self.ref_response if self.ref_response is not None else {
                    "ref": f"refs/tags/{TAG}", "object": {"type": "commit", "sha": self.tag_sha}}
            elif "/releases/assets/" in endpoint:
                asset_id = int(endpoint.rsplit("/", 1)[1])
                assert "Accept: application/octet-stream" in args and method == "GET"
                kwargs["stdout"].write(self.contents[asset_id])
                self.downloaded.append(asset_id)
                return subprocess.CompletedProcess(command, 0, None, "")
            elif endpoint.endswith("/releases/latest"):
                assert method == "GET"
                if self.change_tag_after_latest:
                    self.tag_sha = OLD_SHA
                if self.latest_release is None:
                    return subprocess.CompletedProcess(command, 1, "", "gh: Not Found (HTTP 404)\n")
                value = self.latest_release
            elif endpoint.endswith("/releases/100"):
                assert self.release is not None
                if method == "PATCH":
                    assert self.release["draft"], "Published metadata must remain untouched"
                    assert fields["tag_name"] == TAG
                    assert "draft" not in fields
                    target = fields.get("target_commitish", self.release["target_commitish"])
                    if self.workflows_changed_since_tag and target != self.repository_response["default_branch"]:
                        return subprocess.CompletedProcess(command, 1, "", "gh: Resource not accessible by integration (HTTP 403)")
                    self.release["target_commitish"] = target
                    self.release["body"] = fields["body"]
                    self.release["name"] = fields["name"]
                    self.writes.append("notes")
                value = self.release_response if self.release_response is not None else self.release
            elif endpoint.endswith("/releases"):
                assert method == "POST"
                assert self.tag_sha == SHA and self.release is None
                assert fields["tag_name"] == TAG
                target = fields.get("target_commitish", self.repository_response["default_branch"])
                if self.workflows_changed_since_tag and target != self.repository_response["default_branch"]:
                    return subprocess.CompletedProcess(command, 1, "", "gh: Resource not accessible by integration (HTTP 403)")
                assert fields["draft"] == "true" and fields["prerelease"] == "false"
                assert fields["name"] == f"Chunfe {VERSION}"
                self.existing_release(draft=True, names=[])
                self.release["target_commitish"] = target
                self.release["body"] = fields["body"]
                self.writes.append("create-draft")
                value = self.release
            else:
                raise AssertionError(f"Unexpected API command: {command}")
        elif args[:2] == ["release", "upload"]:
            assert self.release["draft"] and "--clobber" in args
            for argument in args[3:args.index("--repo")]:
                name = Path(argument).name
                self.release["assets"] = [asset for asset in self.release["assets"] if asset["name"] != name]
                data = Path(argument).read_bytes()
                if self.corrupt_upload:
                    data = b"X" * len(data)
                self.add_asset(name, data)
                self.writes.append(f"upload:{name}")
            if self.change_tag_after_upload:
                self.tag_sha = OLD_SHA
            if self.latest_after_upload is not None:
                self.latest_release = copy.deepcopy(self.latest_after_upload)
            if self.publish_after_upload:
                self.release["draft"] = False
            value = ""
        elif args[:2] == ["release", "edit"]:
            assert self.release["draft"], "Published releases must never be edited"
            if (self.workflows_changed_since_tag
                    and self.release["target_commitish"] != self.repository_response["default_branch"]):
                return subprocess.CompletedProcess(command, 1, "", "gh: Resource not accessible by integration (HTTP 403)")
            assert {asset["name"] for asset in self.release["assets"]} == set(NAMES)
            assert "--draft=false" in args and "--prerelease=false" in args
            assert ("--latest" in args) != ("--latest=false" in args)
            if not self.leave_draft:
                self.release["draft"] = False
                self.release["prerelease"] = False
            if "--latest" in args:
                self.latest_release = copy.deepcopy(self.release)
            self.writes.append("publish")
            value = ""
        else:
            raise AssertionError(f"Unexpected command: {command}")
        return subprocess.CompletedProcess(command, 0, value if isinstance(value, str) else json.dumps(value), "")


class PublishReleaseTest(unittest.TestCase):
    def setUp(self):
        spec = importlib.util.spec_from_file_location("publish_release", SCRIPT)
        self.publisher = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.publisher)
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name) / "artifacts"
        self.directory.mkdir()
        self.files = {}
        for name in NAMES:
            self.files[name] = self.directory / name
            self.files[name].write_bytes(name.encode())
        self.fake = FakeGitHub(self.files)
        environment = {"GITHUB_REPOSITORY": REPOSITORY, "GITHUB_SHA": SHA,
                       "GITHUB_REF_TYPE": "tag", "GITHUB_REF_NAME": TAG,
                       "GITHUB_RUN_ID": "1234", "GH_TOKEN": "test-secret",
                       "GITHUB_STEP_SUMMARY": str(Path(self.temporary.name) / "summary")}
        self.environment = patch.dict(os.environ, environment, clear=True)
        self.environment.start()
        self.addCleanup(self.environment.stop)
        process = patch.object(self.publisher.subprocess, "run", self.fake)
        process.start()
        self.addCleanup(process.stop)
        # Source reading is independently tested with real CMake files by test_app_version.
        source = patch.object(self.publisher, "read_version", return_value=VERSION)
        self.read_version = source.start()
        self.addCleanup(source.stop)

    def publish(self):
        with contextlib.redirect_stdout(io.StringIO()) as output:
            self.publisher.publish(self.directory)
        return output.getvalue()

    def test_existing_tag_creates_draft_with_commit_and_run_notes_then_publishes(self):
        self.assertIn(URL, self.publish())
        self.assertEqual(self.fake.tag_sha, SHA)
        self.assertFalse(self.fake.release["draft"])
        self.assertEqual(self.fake.writes[0], "create-draft")
        self.assertEqual(self.fake.writes[-1], "publish")
        self.assertEqual(len(self.fake.release["assets"]), 3)
        self.assertEqual(self.fake.release["target_commitish"], "main")
        self.assertIn(f"<!-- chunfe-release-sha: {SHA} -->", self.fake.release["body"])
        self.assertEqual(self.fake.release["name"], "Chunfe 0.1.0.0")
        self.assertEqual(self.fake.release["body"], NOTES)
        self.assertNotIn("private@example.com", self.fake.release["body"])
        self.assertIn(URL, Path(os.environ["GITHUB_STEP_SUMMARY"]).read_text())

    def test_published_rebuild_keeps_original_release_and_assets(self):
        self.fake.existing_release()
        self.fake.latest_release = {"id": 200, "tag_name": "v0.2.0.0"}
        self.files[NAMES[0]].write_bytes(b"rebuilt bytes differ")
        before, latest = copy.deepcopy(self.fake.release), copy.deepcopy(self.fake.latest_release)
        self.assertIn(URL, self.publish())
        self.assertEqual(self.fake.release, before)
        self.assertEqual(self.fake.latest_release, latest)
        self.assertEqual(self.fake.writes, [])
        self.assertEqual(self.fake.downloaded, [])

    def test_public_release_is_noop_even_with_missing_assets_old_notes_or_immutable_flag(self):
        self.fake.existing_release(names=NAMES[:1], digest=False)
        self.fake.release.update(immutable=True, body="manual release notes", name="manual title", prerelease=True)
        before = copy.deepcopy(self.fake.release)
        self.publish()
        self.assertEqual(self.fake.release, before)
        self.assertEqual(self.fake.writes, [])
        self.assertEqual(self.fake.downloaded, [])

    def test_public_release_does_not_need_repository_latest_or_commit_metadata(self):
        self.fake.existing_release()
        self.fake.failures = {("api", f"repos/{REPOSITORY}"): (1, "timeout"),
                              ("api", f"repos/{REPOSITORY}/releases/latest"): (1, "timeout"),
                              ("api", f"repos/{REPOSITORY}/commits/{SHA}"): (1, "timeout")}
        self.publish()
        self.assertEqual(self.fake.writes, [])

    def test_branch_legacy_or_invalid_tag_context_fails_before_network(self):
        cases = [("GITHUB_REF_TYPE", "branch"), ("GITHUB_REF_TYPE", ""),
                 ("GITHUB_REF_NAME", ""), ("GITHUB_REF_NAME", "main"),
                 ("GITHUB_REF_NAME", "v0.1.0"), ("GITHUB_REF_NAME", "v00.1.0.0"),
                 ("GITHUB_REF_NAME", "v0.1.0.0-beta"), ("GITHUB_REF_NAME", "refs/tags/v0.1.0.0")]
        for key, value in cases:
            with self.subTest(key=key, value=value), patch.dict(os.environ, {key: value}):
                with self.assertRaises((RuntimeError, ValueError)):
                    self.publish()
                self.assertEqual(self.fake.events, [])

    def test_source_tag_and_artifact_versions_must_all_agree(self):
        with patch.dict(os.environ, {"GITHUB_REF_NAME": "v0.1.0.1"}):
            with self.assertRaisesRegex(RuntimeError, "[Vv]ersion|[Tt]ag"):
                self.publish()
        self.read_version.return_value = "0.1.0.1"
        with self.assertRaisesRegex(RuntimeError, "[Vv]ersion|[Tt]ag"):
            self.publish()
        self.assertEqual(self.fake.events, [])

    def test_missing_tag_is_never_created(self):
        self.fake.tag_sha = None
        with self.assertRaises(RuntimeError):
            self.publish()
        self.assertIsNone(self.fake.tag_sha)
        self.assertEqual(self.fake.writes, [])

    def test_tag_at_another_commit_is_never_moved_or_published(self):
        for draft in (True, False):
            with self.subTest(draft=draft):
                self.fake.existing_release(draft=draft, sha=OLD_SHA)
                before = copy.deepcopy(self.fake.release)
                with self.assertRaisesRegex(RuntimeError, "SHA|commit"):
                    self.publish()
                self.assertEqual(self.fake.tag_sha, OLD_SHA)
                self.assertEqual(self.fake.release, before)
                self.assertEqual(self.fake.writes, [])

    def test_annotated_tag_resolves_to_validated_commit(self):
        self.fake.ref_response = {"ref": f"refs/tags/{TAG}", "object": {"type": "tag", "sha": OLD_SHA}}
        self.publish()
        self.assertFalse(self.fake.release["draft"])

    def test_draft_requires_unique_matching_sha_marker_before_any_mutation(self):
        marker = f"<!-- chunfe-release-sha: {SHA} -->"
        for body in (None, NOTES.replace(marker, ""), NOTES.replace(SHA, OLD_SHA),
                     NOTES + marker + "\n", NOTES.replace(marker, "<!-- chunfe-release-sha: invalid -->")):
            with self.subTest(body=body):
                self.fake.existing_release(draft=True)
                self.fake.release["target_commitish"] = SHA
                self.fake.release["body"] = body
                with self.assertRaisesRegex(RuntimeError, "marker|binding"):
                    self.publish()
                self.assertEqual(self.fake.writes, [])

    def test_delayed_release_uses_default_branch_metadata_when_workflows_changed(self):
        self.fake.workflows_changed_since_tag = True
        self.fake.repository_response = {"default_branch": "release/main"}
        try:
            self.publish()
        except RuntimeError as error:
            self.fail(f"Delayed release should publish with the built-in token: {error}")
        self.assertFalse(self.fake.release["draft"])
        self.assertEqual(self.fake.release["target_commitish"], "release/main")
        self.assertIn(f"<!-- chunfe-release-sha: {SHA} -->", self.fake.release["body"])
        self.assertEqual(self.fake.tag_sha, SHA)

    def test_resumed_draft_repairs_old_sha_target_without_moving_tag(self):
        self.fake.existing_release(draft=True, names=NAMES[:1])
        self.fake.release["target_commitish"] = SHA
        self.fake.workflows_changed_since_tag = True
        original_asset = copy.deepcopy(self.fake.release["assets"][0])
        try:
            self.publish()
        except RuntimeError as error:
            self.fail(f"Bound draft should resume after workflow changes: {error}")
        self.assertFalse(self.fake.release["draft"])
        self.assertEqual(self.fake.release["target_commitish"], "main")
        self.assertIn(original_asset, self.fake.release["assets"])
        self.assertNotIn(f"upload:{NAMES[0]}", self.fake.writes)
        self.assertEqual(self.fake.tag_sha, SHA)

    def test_invalid_repository_default_branch_fails_before_any_mutation(self):
        for response in (None, [], {}, {"default_branch": None}, {"default_branch": 123},
                         {"default_branch": ""}, {"default_branch": " "},
                         {"default_branch": "main\n"}):
            with self.subTest(response=response):
                self.fake.repository_response = response
                with self.assertRaisesRegex(RuntimeError, "default branch|repository"):
                    self.publish()
                self.assertEqual(self.fake.writes, [])

    def test_partial_draft_keeps_valid_asset_and_uploads_only_missing_packages(self):
        self.fake.existing_release(draft=True, names=NAMES[:1])
        kept = copy.deepcopy(self.fake.release["assets"][0])
        self.publish()
        self.assertFalse(self.fake.release["draft"])
        self.assertEqual(self.fake.release["assets"][0], kept)
        self.assertNotIn(f"upload:{NAMES[0]}", self.fake.writes)
        self.assertEqual({write for write in self.fake.writes if write.startswith("upload:")},
                         {f"upload:{NAMES[1]}", f"upload:{NAMES[2]}"})

    def test_draft_incomplete_and_checksum_mismatched_assets_are_replaced(self):
        self.fake.existing_release(draft=True)
        self.fake.release["assets"][0].update(state="starter", size=0)
        self.fake.release["assets"][1]["digest"] = "sha256:" + "0" * 64
        kept = copy.deepcopy(self.fake.release["assets"][2])
        self.publish()
        self.assertEqual({write for write in self.fake.writes if write.startswith("upload:")},
                         {f"upload:{NAMES[0]}", f"upload:{NAMES[1]}"})
        self.assertIn(kept, self.fake.release["assets"])
        self.assertFalse(self.fake.release["draft"])

    def test_draft_assets_without_digests_are_downloaded_and_hashed(self):
        self.fake.existing_release(draft=True, digest=False)
        self.publish()
        self.assertTrue({1, 2, 3}.issubset(self.fake.downloaded))
        self.assertFalse(any(write.startswith("upload:") for write in self.fake.writes))

    def test_downloaded_mismatched_draft_asset_is_replaced_and_verified(self):
        self.fake.existing_release(draft=True, digest=False)
        self.fake.contents[1] = b"X" * len(self.fake.contents[1])
        self.publish()
        self.assertIn(f"upload:{NAMES[0]}", self.fake.writes)
        self.assertEqual(self.fake.writes[-1], "publish")

    def test_failed_upload_resumes_same_draft_on_same_tag_and_sha(self):
        self.fake.failures = {("release", "upload"): (1, "upload interrupted")}
        with self.assertRaises(RuntimeError):
            self.publish()
        self.assertTrue(self.fake.release["draft"])
        self.assertEqual(self.fake.release["target_commitish"], "main")
        self.assertIn(f"<!-- chunfe-release-sha: {SHA} -->", self.fake.release["body"])
        self.fake.failures.clear()
        self.fake.writes.clear()
        self.publish()
        self.assertFalse(self.fake.release["draft"])
        self.assertEqual(self.fake.tag_sha, SHA)
        self.assertNotIn("create-draft", self.fake.writes)
        self.assertEqual(self.fake.release["id"], 100)

    def test_tag_change_before_draft_creation_prevents_all_writes(self):
        self.fake.change_tag_after_latest = True
        with self.assertRaisesRegex(RuntimeError, "SHA|commit"):
            self.publish()
        self.assertEqual(self.fake.writes, [])

    def test_release_that_became_public_during_upload_is_not_edited(self):
        self.fake.publish_after_upload = True
        self.publish()
        self.assertNotIn("publish", self.fake.writes)
        self.assertFalse(self.fake.release["draft"])

    def test_numeric_latest_comparison_supports_legacy_and_four_component_versions(self):
        for latest_tag, should_be_latest in (("v0.0.10", True), ("v0.1.0", True),
                ("v0.0.10.99", True), ("v0.1.0.0", True), ("v0.1.0.1", False),
                ("v0.2.0", False), ("v0.10.0.0", False)):
            with self.subTest(latest=latest_tag):
                self.fake.release = None
                self.fake.latest_release = {"id": 200, "tag_name": latest_tag}
                previous = copy.deepcopy(self.fake.latest_release)
                self.publish()
                expected = self.fake.release if should_be_latest else previous
                self.assertEqual(self.fake.latest_release, expected)

    def test_newer_release_published_during_upload_keeps_latest(self):
        self.fake.latest_after_upload = {"id": 200, "tag_name": "v0.2.0.0"}
        self.publish()
        self.assertFalse(self.fake.release["draft"])
        self.assertEqual(self.fake.latest_release, self.fake.latest_after_upload)

    def test_latest_404_allows_first_release_to_become_latest(self):
        self.publish()
        self.assertEqual(self.fake.latest_release["tag_name"], TAG)

    def test_invalid_latest_response_fails_before_any_mutation(self):
        for response in ({}, [], {"tag_name": None}, {"tag_name": 123},
                         {"tag_name": "v0.1"}, {"tag_name": "v01.2.3"},
                         {"tag_name": "v0.1.0.0-beta.1"}, {"tag_name": "other"},
                         "null", "not-json"):
            with self.subTest(response=response):
                self.fake.latest_release = response
                with self.assertRaises((RuntimeError, ValueError)):
                    self.publish()
                self.assertEqual(self.fake.writes, [])

    def test_oversized_utf8_subject_fails_before_mutation(self):
        self.fake.commit_response = copy.deepcopy(COMMIT)
        self.fake.commit_response["commit"]["message"] = "界" * 21000
        with self.assertRaisesRegex(RuntimeError, "60000"):
            self.publish()
        self.assertEqual(self.fake.writes, [])

    def test_invalid_ref_or_release_identity_fails_before_mutation(self):
        self.fake.existing_release()
        for ref in ({}, {"ref": "refs/tags/other", "object": {"type": "commit", "sha": SHA}},
                    {"ref": f"refs/tags/{TAG}", "object": {"type": "commit", "sha": OLD_SHA}},
                    {"ref": f"refs/tags/{TAG}", "object": {"type": "blob", "sha": SHA}}):
            with self.subTest(ref=ref):
                self.fake.ref_response = ref
                with self.assertRaises(RuntimeError):
                    self.publish()
                self.assertEqual(self.fake.writes, [])
        self.fake.ref_response = None
        for changed in ({"id": 999}, {"tag_name": "other"}, {"draft": None}):
            with self.subTest(release=changed):
                self.fake.release_response = {**self.fake.release, **changed}
                with self.assertRaises(RuntimeError):
                    self.publish()
                self.assertEqual(self.fake.writes, [])

    def test_malformed_commit_metadata_fails_before_mutation(self):
        for bad in ({**COMMIT, "commit": None}, {**COMMIT, "sha": "invalid"},
                    {**COMMIT, "commit": {"message": "Subject", "author": {"name": "Author"}}}):
            with self.subTest(commit=bad):
                self.fake.commit_response = bad
                with self.assertRaises(RuntimeError):
                    self.publish()
                self.assertEqual(self.fake.writes, [])

    def test_does_not_swallow_authentication_or_network_errors(self):
        for endpoint in (f"repos/{REPOSITORY}", f"repos/{REPOSITORY}/git/ref/tags/{TAG}",
                         f"repos/{REPOSITORY}/commits/tags/{TAG}",
                         f"repos/{REPOSITORY}/commits/{SHA}", "graphql",
                         f"repos/{REPOSITORY}/releases/latest"):
            for message in ("gh: Bad credentials (HTTP 401)", "gh: Forbidden (HTTP 403)",
                            "gh: Validation failed (HTTP 422)", "dial tcp: timeout",
                            "proxy returned 404 without HTTP status"):
                with self.subTest(endpoint=endpoint, message=message):
                    self.fake.failures = {("api", endpoint): (1, message)}
                    with self.assertRaises(RuntimeError):
                        self.publish()
                    self.assertEqual(self.fake.writes, [])

    def test_failed_publish_is_not_reported_as_success(self):
        self.fake.leave_draft = True
        with self.assertRaisesRegex(RuntimeError, "published"):
            self.publish()
        self.assertFalse(Path(os.environ["GITHUB_STEP_SUMMARY"]).exists())

    def test_first_run_uses_creation_response_when_release_lookup_stays_empty(self):
        self.fake.stale_release_lookup = True
        self.assertIn(URL, self.publish())
        self.assertFalse(self.fake.release["draft"])
        self.assertEqual(sum(command[1:3] == ["api", "graphql"] for command in self.fake.events), 1)

    def test_graphql_errors_or_missing_repository_are_not_missing_release(self):
        for response in ({"data": {"repository": None}},
                         {"errors": [{"message": "forbidden"}], "data": {"repository": {"release": None}}},
                         {"data": {"repository": {}}},
                         {"data": {"repository": {"release": {"databaseId": None}}}}):
            with self.subTest(response=response):
                self.fake.graphql_response = response
                with self.assertRaises(RuntimeError):
                    self.publish()
                self.assertEqual(self.fake.writes, [])

    def test_invalid_artifact_set_fails_before_network(self):
        self.files[NAMES[0]].unlink()
        with self.assertRaises(RuntimeError):
            self.publish()
        self.assertEqual(self.fake.events, [])

    def test_mixed_versions_invalid_names_and_extra_files_fail_before_network(self):
        for name in ("chunfe-0.2.0.0-macos-arm64.dmg", "chunfe-0.1.0.0-beta.1-macos-arm64.dmg",
                     "unexpected.txt"):
            with self.subTest(name=name):
                path = self.directory / name
                self.files[NAMES[0]].rename(path)
                with self.assertRaises(RuntimeError):
                    self.publish()
                path.rename(self.files[NAMES[0]])
        (self.directory / "unexpected.txt").write_text("extra")
        with self.assertRaises(RuntimeError):
            self.publish()
        self.assertEqual(self.fake.events, [])

    def test_wrong_platform_package_formats_fail_before_network(self):
        cases = [
            (NAMES[0], "chunfe-0.1.0.0-macos-arm64.zip"),
            (NAMES[0], "chunfe-0.1.0.0-macos-x64.dmg"),
            (NAMES[0], "chunfe-0.1.0.0-macos-arm64.exe"),
            (NAMES[1], "chunfe-0.1.0.0-windows-x64.exe"),
            (NAMES[1], "chunfe-0.1.0.0-windows-x64-setup.zip"),
            (NAMES[1], "chunfe-0.1.0.0-windows-x64-setup.flatpak"),
            (NAMES[2], "chunfe-0.1.0.0-linux-x64.zip"),
            (NAMES[2], "chunfe-0.1.0.0-linux-arm64.flatpak"),
            (NAMES[2], "chunfe-0.1.0.0-linux-x64.dmg"),
        ]
        for original, invalid in cases:
            with self.subTest(name=invalid):
                path = self.directory / invalid
                self.files[original].rename(path)
                try:
                    with self.assertRaises(RuntimeError):
                        self.publish()
                    self.assertEqual(self.fake.events, [])
                finally:
                    path.rename(self.files[original])

    def test_package_symlinks_and_directories_fail_before_network(self):
        path = self.files[NAMES[0]]
        path.unlink()
        path.symlink_to(self.files[NAMES[1]])
        with self.assertRaises(RuntimeError):
            self.publish()
        self.assertEqual(self.fake.events, [])
        path.unlink()
        path.mkdir()
        with self.assertRaises(RuntimeError):
            self.publish()
        self.assertEqual(self.fake.events, [])

    def test_invalid_run_identity_fails_before_network(self):
        for name, invalid in (("GITHUB_RUN_ID", "42/other-tag"), ("GITHUB_SHA", "invalid"),
                              ("GITHUB_REPOSITORY", "invalid/repo/more")):
            with self.subTest(name=name), patch.dict(os.environ, {name: invalid}):
                with self.assertRaises(RuntimeError):
                    self.publish()
                self.assertEqual(self.fake.events, [])

    def test_unexpected_or_duplicate_remote_assets_fail_without_writes(self):
        self.fake.existing_release(draft=True)
        for name in ("unexpected.zip", NAMES[0]):
            with self.subTest(name=name):
                self.fake.release["assets"].append({"name": name})
                with self.assertRaises(RuntimeError):
                    self.publish()
                self.fake.release["assets"].pop()
                self.assertEqual(self.fake.writes, [])

    def test_tag_change_during_upload_prevents_publish(self):
        self.fake.change_tag_after_upload = True
        with self.assertRaisesRegex(RuntimeError, "SHA"):
            self.publish()
        self.assertNotIn("publish", self.fake.writes)
        self.assertTrue(self.fake.release["draft"])

    def test_uploaded_hash_mismatch_prevents_publish(self):
        self.fake.corrupt_upload = True
        with self.assertRaisesRegex(RuntimeError, "SHA-256"):
            self.publish()
        self.assertNotIn("publish", self.fake.writes)
        self.assertTrue(self.fake.release["draft"])

    def test_commit_response_identity_is_checked_before_mutation(self):
        self.fake.commit_response = {**COMMIT, "sha": OLD_SHA}
        with self.assertRaises(RuntimeError):
            self.publish()
        self.assertEqual(self.fake.writes, [])


if __name__ == "__main__":
    unittest.main()
