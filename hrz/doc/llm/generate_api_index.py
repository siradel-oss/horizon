# SPDX-FileCopyrightText: Copyright 2026 Siradel
# SPDX-License-Identifier: MIT

"""
Generates a compact Markdown API index from hrz_protocol.xml for LLM consumption.
"""

import sys
import re
from pathlib import Path

from hrz.generator import protocol_parser
from hrz.generator.protocol_model import HrzProtocol, HrzMessage

HRZ_PACKAGE_PREFIX = "HrzProtocol."


def short_type(full_type: str) -> str:
    if full_type.startswith(HRZ_PACKAGE_PREFIX):
        return full_type[len(HRZ_PACKAGE_PREFIX) :]
    return full_type


def snake_to_camel(name: str) -> str:
    parts = name.split("_")
    return parts[0] + "".join(p.capitalize() for p in parts[1:])


def pascal_to_camel(name: str) -> str:
    """Proto method names are PascalCase; TypeScript API exposes them as camelCase."""
    return name[0].lower() + name[1:] if name else name


def first_doc_line(documentation: str) -> str:
    if not documentation:
        return ""
    line = documentation.strip().split("\n")[0].strip()
    return re.sub(r"\s+", " ", line)


def find_message(protocol: HrzProtocol, full_name: str) -> HrzMessage | None:
    return next((m for m in protocol.messages if m.full_name == full_name), None)


def generate_markdown(protocol: HrzProtocol) -> str:
    lines: list[str] = [
        "# Horizon API Index",
        "",
        "Proto field names are snake_case; the TypeScript API exposes them as camelCase.",
        "",
    ]

    # ── Section A: Services ──────────────────────────────────────────────────

    lines += [
        "## Services",
        "",
        "Method names are camelCase (TypeScript convention).",
        "",
    ]

    for service in protocol.services:
        lines.append(
            f"### [{service.name}](/doc/reference/HrzProtocol.{service.name}.md)"
        )
        for method in service.methods:
            if method.deprecated:
                continue
            in_t = short_type(method.input)
            out_t = short_type(method.output)
            doc = first_doc_line(method.documentation)
            doc_suffix = f" — {doc}" if doc else ""
            ts_name = pascal_to_camel(method.name)
            lines.append(f"- `{ts_name}({in_t}) → {out_t}`{doc_suffix}")
        lines.append("")

    # ── Section B: Scene model roots ─────────────────────────────────────────

    lines += [
        "## Scene model roots",
        "",
        "Entry points for the path builder API. For layer roots use",
        "`XxxLayerPathBuilder.create(layerHandle)`. For settings roots use",
        "`SceneViewSettingsPathBuilder.create(SceneViewIndex.SCENE_VIEW_0)` etc.",
        "",
    ]

    path_roots = sorted(
        [m for m in protocol.messages if m.is_path_root],
        key=lambda m: m.path_root or "",
    )
    for msg in path_roots:
        param_type = short_type(msg.path_root_type) if msg.path_root_type else "Void"
        lines.append(
            f"- `{msg.path_root}` ({param_type}) → [`{msg.name}`](/doc/reference/HrzProtocol.{msg.name}.md)"
        )
    lines.append("")

    # ── Section C: Message queue messages ────────────────────────────────────

    lines += [
        "## Message queue messages",
        "",
        "Dequeued via `MessageQueueService.dequeueMessages()`. Must be polled every",
        "frame (30–200ms). TS field names are camelCase versions of the proto names below.",
        "",
    ]

    typed_msg = find_message(protocol, "HrzProtocol.TypedMessage")
    if typed_msg:
        for field in typed_msg.fields:
            if field.deprecated:
                continue
            field_type = short_type(field.type)

            if field_type and field_type != "Void":
                lines.append(
                    f"- `{field.name}`, [`{field_type}`](/doc/reference/HrzProtocol.{field_type}.md)"
                )
            else:
                lines.append(f"- `{field.name}`")
    lines.append("")

    return "\n".join(lines)


if __name__ == "__main__":
    protocol_path = sys.argv[1] if len(sys.argv) > 1 else None
    if protocol_path is None:
        raise Exception("Failed to locate hrz_protocol.xml in Bazel runfiles")

    protocol = protocol_parser.parse(protocol_path)
    content = generate_markdown(protocol)

    output_path = sys.argv[2] if len(sys.argv) > 2 else None
    if output_path:
        output_path = Path(output_path)
    else:
        raise Exception("Output path argument is required")

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(content, encoding="utf-8", newline="\n")
