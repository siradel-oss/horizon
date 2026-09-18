# SPDX-FileCopyrightText: Copyright 2026 Siradel
# SPDX-License-Identifier: MIT

import sys
import types
import typing
from enum import Enum, IntEnum, StrEnum

from pydantic.alias_generators import to_camel

from tools.visual_testing.protocol import schema


def _all_models() -> list[type[schema.Model]]:
    return [
        cls
        for cls in vars(schema).values()
        if isinstance(cls, type)
        and issubclass(cls, schema.Model)
        and cls is not schema.Model
    ]


def _all_enums() -> list[type[Enum]]:
    return [
        cls
        for cls in vars(schema).values()
        if isinstance(cls, type)
        and issubclass(cls, Enum)
        and cls not in (Enum, StrEnum, IntEnum)
    ]


def _to_pascal_case(name: str) -> str:
    return "".join(word.capitalize() for word in name.split("_"))


# Returns (type_name, is_optional)
def _ts_type(annotation: type) -> tuple[str, bool]:
    origin: type | None = typing.get_origin(annotation)
    args: tuple[type] = typing.get_args(annotation)

    # Here we try to find types such as "Type | None"
    if origin is types.UnionType:
        not_none = [a for a in args if a is not type(None)]
        if len(not_none) == 1 and type(None) in args:
            return (f"{_ts_type(not_none[0])[0]} | null", True)
        raise TypeError(f"Unsupported union for TS codegen: {annotation!r}")

    if origin is list:
        (item,) = args
        return (f"{_ts_type(item)[0]}[]", False)

    if issubclass(annotation, schema.Model):
        return (annotation.__name__, False)
    if issubclass(annotation, Enum):
        return (annotation.__name__, False)
    if annotation is bool:
        return ("boolean", False)
    if annotation in (int, float):
        return ("number", False)
    if annotation is str:
        return ("string", False)

    raise TypeError(f"Unsupported type for TS codegen: {annotation!r}")


def _render_enum(cls: type[Enum]) -> str:
    if issubclass(cls, IntEnum):
        members = ",\n".join(
            f"    {_to_pascal_case(name)} = {member.value}"
            for name, member in cls.__members__.items()
        )
        return f"export enum {cls.__name__} {{\n{members},\n}}"

    if issubclass(cls, StrEnum):
        values = " | ".join(f'"{member.value}"' for member in cls.__members__.values())
        return f"export type {cls.__name__} = {values};"

    raise TypeError(
        f"Unsupported enum type for TS codegen: {cls.__name__} (expected str values)"
    )


def _render_interface(cls: type[schema.Model]) -> str:
    if not cls.model_fields:
        return f"export interface {cls.__name__} {{}}"

    lines = [f"export interface {cls.__name__} {{"]
    for field_name, field_info in cls.model_fields.items():
        if field_info.annotation is None:
            continue
        ts_name = to_camel(field_name)
        (ts_type, is_optional) = _ts_type(field_info.annotation)
        optional = "?" if (is_optional or not field_info.is_required()) else ""
        lines.append(f"    {ts_name}{optional}: {ts_type};")
    lines.append("}")
    return "\n".join(lines)


def _render_method_map() -> str:
    lines = ["export interface MethodMap {"]
    for name, (request_cls, response_cls) in schema.METHODS.items():
        lines.append(f"    {name}: [{request_cls.__name__}, {response_cls.__name__}];")
    lines.append("}")
    return "\n".join(lines)


def generate() -> str:
    parts: list[str] = []
    parts += [
        _render_enum(cls) for cls in sorted(_all_enums(), key=lambda c: c.__name__)
    ]
    parts += [_render_interface(cls) for cls in _all_models()]
    parts.append(_render_method_map())
    return "\n\n".join(parts) + "\n"


def main():
    content = generate()
    if len(sys.argv) > 1:
        with open(sys.argv[1], "w", encoding="utf8", newline="\n") as f:
            _ = f.write(content)
    else:
        print(content)


if __name__ == "__main__":
    main()
