#!/usr/bin/env python3
"""Publish three validated installers for an existing immutable version tag."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts/ci"))
from app_version import parse_version, read_version


PACKAGE = re.compile(
    r"chunfe-([0-9]+\.[0-9]+\.[0-9]+\.[0-9]+)-"
    r"(macos-arm64\.dmg|windows-x64-setup\.exe|linux-x64\.flatpak)")
PACKAGE_SUFFIXES = {"macos-arm64.dmg", "windows-x64-setup.exe", "linux-x64.flatpak"}


def gh(*arguments, missing=False, output=None):
    result = subprocess.run(
        ["gh", *arguments], env=os.environ.copy(),
        stdout=output if output is not None else subprocess.PIPE,
        stderr=subprocess.PIPE, text=True, check=False)
    if result.returncode:
        if missing and re.search(r"\(HTTP 404\)(?:\s|$)", result.stderr):
            return None
        # Never print the process environment or raw server diagnostics.
        status = re.search(r"\(HTTP ([0-9]{3})\)", result.stderr)
        detail = f"HTTP {status.group(1)}" if status else f"exit {result.returncode}"
        raise RuntimeError(f"gh {' '.join(arguments[:2])} failed ({detail})")
    return result.stdout.strip() if output is None else None


def api(endpoint, *arguments, missing=False):
    result = gh("api", endpoint, *arguments, missing=missing)
    return json.loads(result) if result is not None else None


def find_release(repository, tag):
    owner, name = repository.split("/")
    query = ("query($owner: String!, $name: String!, $tag: String!) {"
             " repository(owner: $owner, name: $name) { release(tagName: $tag) { databaseId } } }")
    response = api("graphql", "-f", f"query={query}", "-f", f"owner={owner}",
                   "-f", f"name={name}", "-f", f"tag={tag}")
    if (not isinstance(response, dict) or response.get("errors")
            or not isinstance(response.get("data"), dict)):
        raise RuntimeError("GraphQL release lookup failed")
    result = response["data"].get("repository")
    if not isinstance(result, dict) or "release" not in result:
        raise RuntimeError("GraphQL repository lookup failed")
    if result["release"] is None:
        return None
    release = result["release"]
    release_id = release.get("databaseId") if isinstance(release, dict) else None
    if type(release_id) is not int or release_id <= 0:
        raise RuntimeError("GraphQL returned an invalid release ID")
    release = api(f"repos/{repository}/releases/{release_id}")
    if not isinstance(release, dict) or release.get("id") != release_id:
        raise RuntimeError("GitHub returned an unexpected release ID")
    return release


def verify_assets(release, files, repository, *, complete, replace=False):
    assets = release["assets"]
    names = [asset["name"] for asset in assets]
    if len(names) != len(set(names)) or set(names) - files.keys():
        raise RuntimeError("Release contains duplicate or unexpected assets")
    if complete and set(names) != files.keys():
        raise RuntimeError("Release does not contain the complete three platform packages")
    verified = set()
    for asset in assets:
        path = files[asset["name"]]
        expected = hashlib.sha256(path.read_bytes()).hexdigest()
        if asset["state"] != "uploaded" or asset["size"] != path.stat().st_size:
            if replace:
                continue
            raise RuntimeError(f"Incomplete or different asset: {path.name}")
        digest = asset.get("digest")
        if not digest:
            with tempfile.TemporaryFile() as downloaded:
                gh("api", f"repos/{repository}/releases/assets/{asset['id']}",
                   "-H", "Accept: application/octet-stream", output=downloaded)
                downloaded.seek(0)
                digest = "sha256:" + hashlib.sha256(downloaded.read()).hexdigest()
        if digest != "sha256:" + expected:
            if replace:
                continue
            raise RuntimeError(f"Asset SHA-256 differs: {path.name}")
        verified.add(path.name)
    return verified


def commit_details(commit, repository, *, sha=None):
    if not isinstance(commit, dict):
        raise RuntimeError("GitHub returned invalid commit metadata")
    commit_sha = commit.get("sha", "")
    if (not isinstance(commit_sha, str) or not re.fullmatch(r"[0-9a-fA-F]{40}", commit_sha)
            or (sha is not None and commit_sha.lower() != sha.lower())):
        raise RuntimeError("GitHub returned an unexpected commit SHA")
    details = commit.get("commit")
    author = details.get("author") if isinstance(details, dict) else None
    if (not isinstance(author, dict) or not isinstance(details.get("message"), str)
            or any(not isinstance(author.get(key), str) or not author[key] for key in ("name", "date"))):
        raise RuntimeError("GitHub returned incomplete commit metadata")
    # Read only the subject and public attribution, never email or raw API output.
    lines = details["message"].splitlines()
    return (f"[{commit_sha}](https://github.com/{repository}/commit/{commit_sha})",
            lines[0] if lines else "(no subject)", author["name"], author["date"])


def verify_tag(repository, tag, sha):
    # An absent tag is an error: this publisher is never allowed to create one.
    ref = api(f"repos/{repository}/git/ref/tags/{tag}")
    obj = ref.get("object") if isinstance(ref, dict) else None
    if (not isinstance(ref, dict) or ref.get("ref") != f"refs/tags/{tag}"
            or not isinstance(obj, dict) or obj.get("type") not in {"commit", "tag"}
            or not isinstance(obj.get("sha"), str)
            or not re.fullmatch(r"[0-9a-fA-F]{40}", obj["sha"])):
        raise RuntimeError("GitHub returned an unexpected tag ref")
    # The commits endpoint peels annotated tags; an object's SHA may be a tag object.
    target = gh("api", f"repos/{repository}/commits/tags/{tag}", "--jq", ".sha").lower()
    if (target != sha or (obj["type"] == "commit" and obj["sha"].lower() != sha)):
        raise RuntimeError("Existing tag SHA does not match GITHUB_SHA")


def validate_release(release, tag, sha, *, release_id=None):
    if (not isinstance(release, dict) or type(release.get("id")) is not int
            or release["id"] <= 0 or (release_id is not None and release["id"] != release_id)
            or release.get("tag_name") != tag or type(release.get("draft")) is not bool
            or type(release.get("prerelease")) is not bool):
        raise RuntimeError("GitHub returned an unexpected release identity or state")
    if release["draft"]:
        body = release.get("body")
        markers = re.findall(r"(?m)^<!-- chunfe-release-sha: (.*) -->$", body) if isinstance(body, str) else []
        if markers != [sha]:
            raise RuntimeError("Draft release SHA marker does not match GITHUB_SHA")


def latest_option(repository, version_parts):
    latest_flag = "--latest"
    latest_response = gh("api", f"repos/{repository}/releases/latest", missing=True)
    if latest_response is not None:
        latest = json.loads(latest_response)
        latest_tag = latest.get("tag_name") if isinstance(latest, dict) else None
        if not isinstance(latest_tag, str) or not latest_tag.startswith("v"):
            raise RuntimeError("GitHub returned an invalid latest release tag")
        latest_parts = parse_version(latest_tag[1:], allow_legacy=True)
        latest_parts += (0,) * (4 - len(latest_parts))
        if latest_parts > version_parts:
            latest_flag = "--latest=false"

    return latest_flag


def report(tag, release, *, existing=False):
    url = release["html_url"]
    heading = "Chunfe release already published; unchanged" if existing else "Published Chunfe release"
    print(f"Already published; unchanged: {url}" if existing else url)
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a", encoding="utf-8") as stream:
            stream.write(f"### {heading}\n\n[{tag}]({url})\n")


def publish(directory):
    paths = sorted(Path(directory).iterdir())
    matches = [PACKAGE.fullmatch(path.name) for path in paths]
    if (len(paths) != 3 or not all(matches)
            or any(not path.is_file() or path.is_symlink() for path in paths)
            or {match[2] for match in matches} != PACKAGE_SUFFIXES
            or len({match[1] for match in matches}) != 1):
        raise RuntimeError(
            "Expected exactly one macOS ARM64 DMG, Windows x64 setup EXE and Linux x64 Flatpak "
            "with one numeric major.minor.patch.build version")
    version = matches[0][1]
    version_parts = parse_version(version)
    files = {path.name: path.resolve() for path in paths}
    values = {}
    for key, pattern in {
        "GITHUB_REPOSITORY": r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+",
        "GITHUB_SHA": r"[0-9a-fA-F]{40}",
        "GITHUB_RUN_ID": r"[1-9][0-9]*",
        "GITHUB_REF_TYPE": r"tag",
        "GITHUB_REF_NAME": r"v[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+",
    }.items():
        values[key] = os.environ.get(key, "")
        if not re.fullmatch(pattern, values[key]):
            raise RuntimeError(f"Missing or invalid {key}")
    tag = values["GITHUB_REF_NAME"]
    parse_version(tag[1:])
    if tag != f"v{version}" or read_version(ROOT) != version:
        raise RuntimeError("Tag, source version and all three artifact versions must match")
    repository, sha = values["GITHUB_REPOSITORY"], values["GITHUB_SHA"].lower()
    verify_tag(repository, tag, sha)
    release = find_release(repository, tag)
    if release is not None:
        validate_release(release, tag, sha)
        # Successful versions are immutable, including when rebuilt bytes differ.
        if not release["draft"]:
            report(tag, release, existing=True)
            return

    metadata = api(f"repos/{repository}")
    default_branch = metadata.get("default_branch") if isinstance(metadata, dict) else None
    if (not isinstance(default_branch, str) or not default_branch
            or any(character.isspace() for character in default_branch)):
        raise RuntimeError("GitHub returned an invalid repository default branch")
    # target_commitish does not move an existing tag. Use the default branch so
    # GITHUB_TOKEN needs no Workflows permission when retrying an older tag.
    # The body marker binds a draft to its original SHA independently of this field.
    commit = api(f"repos/{repository}/commits/{sha}")
    link, subject, author, date = commit_details(commit, repository, sha=sha)
    notes = (f"Chunfe version: {version}\nCommit: {link}\n"
             f"Subject: {subject}\nAuthor: {author}\nDate: {date}\n\n"
             f"Workflow run: https://github.com/{repository}/actions/runs/{values['GITHUB_RUN_ID']}\n\n"
             f"<!-- chunfe-release-sha: {sha} -->\n")
    if len(notes.encode("utf-8")) > 60000:
        raise RuntimeError("Release notes exceed 60000 UTF-8 bytes")
    title = f"Chunfe {version}"
    present = verify_assets(release, files, repository, complete=False, replace=True) if release else set()
    latest_option(repository, version_parts)

    verify_tag(repository, tag, sha)
    if release is None:
        # Consume the creation response: GraphQL/list reads may lag a successful write.
        release = api(f"repos/{repository}/releases", "--method", "POST",
                      "-f", f"tag_name={tag}", "-f", f"target_commitish={default_branch}",
                      "-f", f"name={title}", "-f", f"body={notes}",
                      "-F", "draft=true", "-F", "prerelease=false")
        validate_release(release, tag, sha)
    else:
        release_id = release["id"]
        release = api(f"repos/{repository}/releases/{release_id}")
        validate_release(release, tag, sha, release_id=release_id)
        if not release["draft"]:
            report(tag, release, existing=True)
            return
        release = api(f"repos/{repository}/releases/{release_id}", "--method", "PATCH",
                      "-f", f"tag_name={tag}", "-f", f"target_commitish={default_branch}",
                      "-f", f"name={title}", "-f", f"body={notes}")
        validate_release(release, tag, sha, release_id=release_id)
    if not release["draft"]:
        report(tag, release, existing=True)
        return
    release_id = release["id"]
    release_endpoint = f"repos/{repository}/releases/{release_id}"
    missing = [str(path) for name, path in files.items() if name not in present]
    if missing:
        gh("release", "upload", tag, *missing, "--repo", repository, "--clobber")
    release = api(release_endpoint)
    validate_release(release, tag, sha, release_id=release_id)
    if not release["draft"]:
        report(tag, release, existing=True)
        return
    verify_assets(release, files, repository, complete=True)
    # Another version may have published while these packages uploaded.
    latest_flag = latest_option(repository, version_parts)
    verify_tag(repository, tag, sha)
    gh("release", "edit", tag, "--repo", repository,
       "--draft=false", "--prerelease=false", latest_flag)
    release = api(release_endpoint)
    validate_release(release, tag, sha, release_id=release_id)
    if release["draft"] or release["prerelease"]:
        raise RuntimeError("Release did not become a published ordinary release")
    report(tag, release)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("artifact_directory", type=Path)
    arguments = parser.parse_args()
    try:
        publish(arguments.artifact_directory)
    except (RuntimeError, OSError, ValueError, KeyError) as error:
        print(f"Release publishing failed: {error}", file=sys.stderr)
        sys.exit(1)
