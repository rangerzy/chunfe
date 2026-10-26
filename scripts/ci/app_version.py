#!/usr/bin/env python3
"""Read the release version without configuring Qt or evaluating CMake."""

from __future__ import annotations

import argparse
from pathlib import Path
import re


COMPONENTS = ("MAJOR", "MINOR", "PATCH", "BUILD")
COMPONENT_PATTERN = r"(?:0|[1-9][0-9]{0,4})"
VERSION_PATTERN = re.compile(rf"{COMPONENT_PATTERN}(?:\.{COMPONENT_PATTERN}){{3}}")
LEGACY_PATTERN = re.compile(rf"{COMPONENT_PATTERN}(?:\.{COMPONENT_PATTERN}){{2,3}}")
DECLARATION_START = re.compile(r"^[ \t]*(?i:set)[ \t]*\([ \t]*VERSION_(MAJOR|MINOR|PATCH|BUILD)\b")
DECLARATION = re.compile(r"[ \t]*(?i:set)[ \t]*\([ \t]*VERSION_(MAJOR|MINOR|PATCH|BUILD)[ \t]+([0-9]+)[ \t]*\)[ \t]*(?:#.*)?")


def parse_version(value: str, *, allow_legacy: bool = False) -> tuple[int, ...]:
    """Validate canonical decimal components, bounded by Windows version fields."""
    pattern = LEGACY_PATTERN if allow_legacy else VERSION_PATTERN
    if not pattern.fullmatch(value):
        raise ValueError(f"Invalid application version: {value!r}; expected {'three or four' if allow_legacy else 'four'} canonical numeric components")
    parts = tuple(int(part) for part in value.split("."))
    if any(part > 65535 for part in parts):
        raise ValueError(f"Invalid application version: {value!r}; each component must be between 0 and 65535")
    return parts


def version_from_source(source: str) -> str:
    """Read exactly one literal default for each component from AppVersion.cmake."""
    values = {}
    for line in source.splitlines():
        if not DECLARATION_START.match(line):
            continue
        match = DECLARATION.fullmatch(line)
        if not match:
            raise ValueError(f"Invalid version component declaration: {line.strip()}")
        name, value = match.groups()
        if name in values:
            raise ValueError(f"Duplicate VERSION_{name} declaration")
        values[name] = value
    missing = [f"VERSION_{name}" for name in COMPONENTS if name not in values]
    if missing:
        raise ValueError(f"Missing version component declarations: {', '.join(missing)}")
    version = ".".join(values[name] for name in COMPONENTS)
    parse_version(version)
    return version


def read_version(root: Path) -> str:
    return version_from_source((root / "cmake/AppVersion.cmake").read_text(encoding="utf-8"))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check-tag", help="require a tag named v<declared version>")
    args = parser.parse_args()
    try:
        version = read_version(Path(__file__).resolve().parents[2])
        if args.check_tag is not None:
            if not args.check_tag.startswith("v"):
                raise ValueError("Release tag must start with 'v'")
            parse_version(args.check_tag[1:])
            if args.check_tag != f"v{version}":
                raise ValueError(f"Release tag {args.check_tag!r} does not match declared version v{version}")
    except (OSError, ValueError) as error:
        parser.error(str(error))
    print(version)


if __name__ == "__main__":
    main()
