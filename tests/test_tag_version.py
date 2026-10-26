"""Main-push version detection against real Git history and a fake GitHub boundary."""
import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts/ci'))
SPEC = importlib.util.spec_from_file_location('tag_version', ROOT / '.github/scripts/tag_version.py')


def version_source(value):
    return '\n'.join(f'set(VERSION_{key} {part})' for key, part in zip(('MAJOR', 'MINOR', 'PATCH', 'BUILD'), value.split('.'))) + '\n'


class Remote:
    def __init__(self):
        self.tags = {}
        self.public = False
        self.writes = []
        self.fail_dispatch = False
        self.fail_read = False
        self.lost_creation_reply = False

    def __call__(self, endpoint, *, method='GET', payload=None, missing=False):
        if self.fail_read:
            raise RuntimeError('GitHub read failed (HTTP 403)')
        if '/git/ref/tags/' in endpoint:
            tag = endpoint.rsplit('/', 1)[1]
            if tag not in self.tags:
                assert missing
                return None
            return {'ref': 'refs/tags/' + tag, 'object': {'type': 'commit', 'sha': self.tags[tag]}}
        if '/commits/tags/' in endpoint:
            return {'sha': self.tags[endpoint.rsplit('/', 1)[1]]}
        if endpoint.endswith('/git/matching-refs/tags/'):
            return [{'ref': 'refs/tags/' + tag, 'object': {'type': 'commit', 'sha': sha}} for tag, sha in self.tags.items()]
        if '/releases/tags/' in endpoint:
            return {'tag_name': endpoint.rsplit('/', 1)[1], 'draft': False} if self.public else None
        if endpoint.endswith('/git/refs'):
            assert method == 'POST' and set(payload) == {'ref', 'sha'}
            tag = payload['ref'].removeprefix('refs/tags/')
            assert tag not in self.tags
            self.tags[tag] = payload['sha']
            self.writes.append(('tag', tag, payload['sha']))
            if self.lost_creation_reply:
                self.lost_creation_reply = False
                raise RuntimeError('Connection lost after tag creation')
            return {'ref': payload['ref'], 'object': {'sha': payload['sha'], 'type': 'commit'}}
        if endpoint.endswith('/actions/workflows/build.yml/dispatches'):
            assert method == 'POST' and set(payload) == {'ref'}
            if self.fail_dispatch:
                raise RuntimeError('Dispatch temporarily failed')
            self.writes.append(('dispatch', payload['ref']))
            return {'workflow_run_id': 123}
        raise AssertionError((endpoint, method, payload))


class TagVersionTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.module = importlib.util.module_from_spec(SPEC)
        SPEC.loader.exec_module(cls.module)

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.git('init', '-q')
        self.git('config', 'user.name', 'Test')
        self.git('config', 'user.email', 'test@example.invalid')
        self.remote = Remote()
        self.env = patch.dict(os.environ, {'GITHUB_REPOSITORY': 'example/chunfe', 'GITHUB_EVENT_NAME': 'push', 'GITHUB_REF': 'refs/heads/main'}, clear=True)
        self.env.start()
        self.addCleanup(self.env.stop)
        self.api = patch.object(self.module, 'api', self.remote)
        self.api.start()
        self.addCleanup(self.api.stop)

    def git(self, *args):
        return subprocess.check_output(['git', '-C', str(self.root), *args], stderr=subprocess.PIPE).decode().strip()

    def commit(self, version, *, legacy=False):
        file = self.root / ('CMakeLists.txt' if legacy else 'cmake/AppVersion.cmake')
        file.parent.mkdir(parents=True, exist_ok=True)
        file.write_text(f'project(Chunfe VERSION {version} LANGUAGES CXX)\n' if legacy else version_source(version))
        self.git('add', '.')
        self.git('commit', '-qm', 'Version ' + version, '--allow-empty')
        return self.git('rev-parse', 'HEAD')

    def run_push(self, before, after):
        os.environ['GITHUB_SHA'] = after
        with contextlib.redirect_stdout(io.StringIO()):
            return self.module.coordinate(self.root, {'before': before, 'after': after, 'ref': 'refs/heads/main'})

    def test_version_bump_tags_exact_push_commit_then_dispatches_tag(self):
        before = self.commit('0.1.0.0')
        after = self.commit('0.1.0.1')
        self.assertEqual(self.run_push(before, after), 'v0.1.0.1')
        self.assertEqual(self.remote.writes, [('tag', 'v0.1.0.1', after), ('dispatch', 'v0.1.0.1')])

    def test_unchanged_version_does_not_write_even_without_tag(self):
        before = self.commit('0.1.0.0')
        after = self.commit('0.1.0.0')
        self.assertIsNone(self.run_push(before, after))
        self.assertEqual(self.remote.writes, [])

    def test_legacy_three_part_version_can_migrate_to_four_part(self):
        before = self.commit('0.1.0', legacy=True)
        after = self.commit('0.1.0.0')
        self.remote.tags['v0.1.0'] = before
        self.run_push(before, after)
        self.assertEqual(self.remote.tags['v0.1.0'], before)
        self.assertEqual(self.remote.tags['v0.1.0.0'], after)

    def test_regression_is_rejected_before_writes(self):
        before = self.commit('0.2.0.0')
        after = self.commit('0.1.0.9')
        with self.assertRaisesRegex(RuntimeError, '[Vv]ersion.*(older|decreas|regress)'):
            self.run_push(before, after)
        self.assertEqual(self.remote.writes, [])

    def test_existing_tag_for_other_commit_is_not_moved_or_dispatched(self):
        before = self.commit('0.1.0.0')
        after = self.commit('0.1.0.1')
        self.remote.tags['v0.1.0.1'] = before
        with self.assertRaisesRegex(RuntimeError, '(different|another).*commit'):
            self.run_push(before, after)
        self.assertEqual(self.remote.writes, [])

    def test_failed_dispatch_retry_keeps_tag_and_dispatches_original_commit(self):
        before = self.commit('0.1.0.0')
        after = self.commit('0.1.0.1')
        self.remote.fail_dispatch = True
        with self.assertRaisesRegex(RuntimeError, 'Dispatch'):
            self.run_push(before, after)
        self.remote.fail_dispatch = False
        self.run_push(before, after)
        self.assertEqual(self.remote.writes, [('tag', 'v0.1.0.1', after), ('dispatch', 'v0.1.0.1')])

    def test_lost_tag_creation_reply_can_be_retried_without_moving_tag(self):
        before = self.commit('0.1.0.0')
        after = self.commit('0.1.0.1')
        self.remote.lost_creation_reply = True
        with self.assertRaisesRegex(RuntimeError, 'Connection lost'):
            self.run_push(before, after)
        self.run_push(before, after)
        self.assertEqual(self.remote.writes, [('tag', 'v0.1.0.1', after), ('dispatch', 'v0.1.0.1')])

    def test_published_version_retry_does_not_dispatch(self):
        before = self.commit('0.1.0.0')
        after = self.commit('0.1.0.1')
        self.remote.tags['v0.1.0.1'] = after
        self.remote.public = True
        self.run_push(before, after)
        self.assertEqual(self.remote.writes, [])

    def test_remote_newer_version_blocks_older_unissued_version(self):
        before = self.commit('0.1.0.0')
        after = self.commit('0.1.0.1')
        self.remote.tags['v0.1.0.2'] = before
        with self.assertRaisesRegex(RuntimeError, '[Vv]ersion.*(older|newer|regress)'):
            self.run_push(before, after)
        self.assertEqual(self.remote.writes, [])

    def test_missing_before_commit_fails_without_creating_tag(self):
        after = self.commit('0.1.0.1')
        with self.assertRaisesRegex(RuntimeError, '(previous|before|predecessor)'):
            self.run_push('a' * 40, after)
        self.assertEqual(self.remote.writes, [])

    def test_initial_main_creation_can_issue_declared_version(self):
        after = self.commit('0.1.0.0')
        self.run_push('0' * 40, after)
        self.assertEqual(self.remote.writes, [('tag', 'v0.1.0.0', after), ('dispatch', 'v0.1.0.0')])

    def test_api_failure_does_not_create_a_tag(self):
        before = self.commit('0.1.0.0')
        after = self.commit('0.1.0.1')
        self.remote.fail_read = True
        with self.assertRaisesRegex(RuntimeError, '403'):
            self.run_push(before, after)
        self.assertEqual(self.remote.writes, [])

    def test_non_main_event_is_rejected(self):
        after = self.commit('0.1.0.0')
        os.environ['GITHUB_REF'] = 'refs/heads/dev'
        with self.assertRaisesRegex(RuntimeError, 'main'):
            self.run_push('0' * 40, after)
        self.assertEqual(self.remote.writes, [])

    def test_checkout_must_match_event_commit(self):
        before = self.commit('0.1.0.0')
        self.commit('0.1.0.1')
        with self.assertRaisesRegex(RuntimeError, '(checkout|HEAD)'):
            self.run_push('0' * 40, before)
        self.assertEqual(self.remote.writes, [])


class GitHubBoundaryTest(unittest.TestCase):
    def setUp(self):
        self.module = importlib.util.module_from_spec(SPEC)
        SPEC.loader.exec_module(self.module)

    def test_only_http_404_is_absent_and_auth_errors_propagate(self):
        for message, absent in [('gh: Not Found (HTTP 404)\n', True),
                                ('gh: Forbidden (HTTP 403)\n', False),
                                ('gh: Validation failed (HTTP 422)\n', False),
                                ('upstream text mentions 404 but timed out', False)]:
            with self.subTest(message=message), patch.object(self.module.subprocess, 'run', return_value=subprocess.CompletedProcess([], 1, '', message)):
                if absent:
                    self.assertIsNone(self.module.api('repos/example/chunfe/git/ref/tags/v0.1.0.0', missing=True))
                else:
                    with self.assertRaises(RuntimeError):
                        self.module.api('repos/example/chunfe/git/ref/tags/v0.1.0.0', missing=True)

    def test_dispatch_posts_json_ref_and_accepts_empty_success_response(self):
        def process(command, **kwargs):
            self.assertEqual(command, ['gh', 'api', 'repos/example/chunfe/actions/workflows/build.yml/dispatches', '--method', 'POST', '--input', '-'])
            self.assertEqual(json.loads(kwargs['input']), {'ref': 'v0.1.0.1'})
            return subprocess.CompletedProcess(command, 0, '', '')
        with patch.object(self.module.subprocess, 'run', side_effect=process):
            self.assertIsNone(self.module.api('repos/example/chunfe/actions/workflows/build.yml/dispatches', method='POST', payload={'ref':'v0.1.0.1'}))


if __name__ == '__main__':
    unittest.main()
