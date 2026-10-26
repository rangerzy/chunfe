#!/usr/bin/env python3
"""Issue a new version tag on main and dispatch the tag's release workflow."""
import json
import os
from pathlib import Path
import re
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts/ci'))
from app_version import parse_version, read_version, version_from_source


class GitHubError(RuntimeError):
    def __init__(self, endpoint, status):
        self.status = status
        super().__init__(f'GitHub request to {endpoint} failed ({status})')


def api(endpoint, *, method='GET', payload=None, missing=False):
    command = ['gh', 'api', endpoint, '--method', method]
    if payload is not None:
        command += ['--input', '-']
    result = subprocess.run(command, input=json.dumps(payload) if payload is not None else None,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=False)
    if result.returncode:
        match = re.search(r'\(HTTP ([0-9]{3})\)(?:\s|$)', result.stderr)
        status = f'HTTP {match[1]}' if match else f'exit {result.returncode}'
        if missing and status == 'HTTP 404':
            return None
        raise GitHubError(endpoint, status)
    return json.loads(result.stdout) if result.stdout.strip() else None


def git(root, *arguments, optional=False):
    result = subprocess.run(['git', '-C', str(root), *arguments], stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, text=True, check=False)
    if result.returncode:
        if optional:
            return None
        raise RuntimeError(f"Cannot read Git checkout: git {' '.join(arguments)}")
    return result.stdout.strip()


def previous_version(root, before):
    if before == '0' * 40:
        return None
    if git(root, 'cat-file', '-e', before + '^{commit}', optional=True) is None:
        raise RuntimeError('Cannot read the previous main commit; fetch its history before retrying')
    source = git(root, 'show', before + ':cmake/AppVersion.cmake', optional=True)
    if source is not None:
        return version_from_source(source)
    source = git(root, 'show', before + ':CMakeLists.txt', optional=True)
    if source is None:
        raise RuntimeError('Cannot read the previous declared version')
    matches = re.findall(r'(?m)^\s*project\(\s*Chunfe\s+VERSION\s+([0-9.]+)\s+LANGUAGES\s+CXX\s*\)', source)
    if len(matches) == 1:
        parse_version(matches[0], allow_legacy=True)
        return matches[0]
    # Also handle the user's pre-extraction four-component declaration.
    return version_from_source(source)


def padded(value):
    parts = parse_version(value, allow_legacy=True)
    return parts + (0,) * (4 - len(parts))


def tag_commit(repository, tag):
    ref = api(f'repos/{repository}/git/ref/tags/{tag}', missing=True)
    if ref is None:
        return None
    if not isinstance(ref, dict) or ref.get('ref') != 'refs/tags/' + tag:
        raise RuntimeError('GitHub returned an unexpected tag ref')
    commit = api(f'repos/{repository}/commits/tags/{tag}')
    sha = commit.get('sha') if isinstance(commit, dict) else None
    if not isinstance(sha, str) or not re.fullmatch(r'[0-9a-f]{40}', sha):
        raise RuntimeError('GitHub returned an invalid tag commit')
    return sha


def coordinate(root, event):
    repository = os.environ.get('GITHUB_REPOSITORY', '')
    sha = os.environ.get('GITHUB_SHA', '')
    if (os.environ.get('GITHUB_EVENT_NAME') != 'push'
            or os.environ.get('GITHUB_REF') != 'refs/heads/main'
            or event.get('ref') != 'refs/heads/main'):
        raise RuntimeError('Version tags can only be created by a main push')
    before = event.get('before', '')
    if (not re.fullmatch(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+', repository)
            or not re.fullmatch(r'[0-9a-f]{40}', sha)
            or not isinstance(before, str) or not re.fullmatch(r'[0-9a-f]{40}', before)
            or event.get('after') != sha):
        raise RuntimeError('Invalid repository or push commit metadata')
    if git(root, 'rev-parse', 'HEAD') != sha:
        raise RuntimeError('Git checkout HEAD does not match the push commit')
    version = read_version(root)
    old = previous_version(root, before)
    if old == version:
        print(f'APP_VERSION is unchanged ({version}); no release requested.')
        return None
    if old is not None and padded(version) < padded(old):
        raise RuntimeError('Version decreased relative to the previous main commit')
    tag = 'v' + version
    existing = tag_commit(repository, tag)
    if existing is not None and existing != sha:
        raise RuntimeError(f'{tag} already belongs to a different commit; increase APP_VERSION')
    if existing is None:
        refs = api(f'repos/{repository}/git/matching-refs/tags/')
        if not isinstance(refs, list):
            raise RuntimeError('GitHub returned an invalid tag list')
        for ref in refs:
            name = ref.get('ref') if isinstance(ref, dict) else None
            if not isinstance(name, str) or not name.startswith('refs/tags/'):
                raise RuntimeError('GitHub returned an invalid tag ref')
            candidate = name.removeprefix('refs/tags/v')
            if not re.fullmatch(r'[0-9]+(?:\.[0-9]+){2,3}', candidate):
                continue
            if padded(candidate) > padded(version):
                raise RuntimeError('Version is older than an existing version tag')
        try:
            api(f'repos/{repository}/git/refs', method='POST', payload={'ref': 'refs/tags/' + tag, 'sha': sha})
        except GitHubError as error:
            # A simultaneous creator may win. Verify its exact target, never force-update.
            if error.status != 'HTTP 422' or tag_commit(repository, tag) != sha:
                raise
    if tag_commit(repository, tag) != sha:
        raise RuntimeError('Tag changed before workflow dispatch')
    release = api(f'repos/{repository}/releases/tags/{tag}', missing=True)
    if release is not None:
        if (not isinstance(release, dict) or release.get('tag_name') != tag
                or type(release.get('draft')) is not bool):
            raise RuntimeError('GitHub returned an invalid release')
        if not release['draft']:
            print(f'{tag} is already published; leaving it unchanged.')
            return tag
    # GITHUB_TOKEN-generated push events do not start workflows. Dispatch explicitly.
    api(f'repos/{repository}/actions/workflows/build.yml/dispatches', method='POST', payload={'ref': tag})
    print(f'Dispatched build.yml for {tag} at {sha}.')
    return tag


if __name__ == '__main__':
    try:
        event = json.loads(Path(os.environ['GITHUB_EVENT_PATH']).read_text(encoding='utf-8'))
        coordinate(Path(__file__).resolve().parents[2], event)
    except (KeyError, ValueError, OSError, RuntimeError) as error:
        sys.exit(f'Version release preparation failed: {error}')
