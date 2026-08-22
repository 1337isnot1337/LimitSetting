#!/usr/bin/env python3
"""Merge generated param nuisances into a Combine datacard safely."""

from __future__ import annotations

import argparse
from pathlib import Path
import re


PARAM_LINE = re.compile(
    r"^(?P<name>\S+)\s+param\s+[-+]?\d+(?:\.\d+)?(?:[eE][-+]?\d+)?\s+"
    r"[-+]?\d+(?:\.\d+)?(?:[eE][-+]?\d+)?(?:\s+#.*)?$"
)


def read_param_lines(path: Path) -> list[str]:
    result: list[str] = []
    seen: set[str] = set()
    for raw_line in path.read_text().splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        match = PARAM_LINE.match(line)
        if not match:
            raise ValueError(f"Unsupported nuisance line in {path}: {raw_line!r}")
        name = match.group("name")
        if name in seen:
            raise ValueError(f"Duplicate nuisance {name!r} in {path}")
        seen.add(name)
        result.append(line)
    if not result:
        raise ValueError(f"No param nuisance lines found in {path}")
    return result


def merge(card_text: str, param_lines: list[str]) -> str:
    names = {PARAM_LINE.match(line).group("name") for line in param_lines}  # type: ignore[union-attr]
    output: list[str] = []
    found_kmax = False

    for line in card_text.splitlines():
        if re.match(r"^\s*kmax\s+", line):
            line = re.sub(r"^(\s*kmax\s+)\S+", r"\1*", line, count=1)
            found_kmax = True

        fields = line.split()
        if len(fields) >= 2 and fields[0] in names and fields[1] == "param":
            continue
        output.append(line)

    if not found_kmax:
        raise ValueError("Datacard has no kmax line")

    while output and not output[-1].strip():
        output.pop()
    output.extend(
        [
            "",
            "# Shape nuisances embedded in the parametric signal workspace",
            *param_lines,
            "",
        ]
    )
    return "\n".join(output)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Add the generated zero-centred shape nuisance parameters to a Combine datacard."
    )
    parser.add_argument("datacard", type=Path)
    parser.add_argument("nuisance_lines", type=Path)
    destination = parser.add_mutually_exclusive_group(required=True)
    destination.add_argument("--output", type=Path)
    destination.add_argument("--in-place", action="store_true")
    args = parser.parse_args()

    param_lines = read_param_lines(args.nuisance_lines)
    merged = merge(args.datacard.read_text(), param_lines)
    output = args.datacard if args.in_place else args.output
    output.write_text(merged)
    print(f"Wrote {len(param_lines)} shape nuisance(s) to {output}")


if __name__ == "__main__":
    main()
