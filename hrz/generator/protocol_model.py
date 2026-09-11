# SPDX-FileCopyrightText: Copyright 2026 Siradel
# SPDX-License-Identifier: MIT

from dataclasses import dataclass
from typing import List, Optional


@dataclass
class HrzTopLevelEntity:
    """Base class for top-level protocol entities (messages, enums, services)."""

    file: str
    full_name: str
    name: str
    package: str
    documentation: str


@dataclass
class HrzField:
    """Represents a field within a message."""

    name: str
    id: int
    type: str
    repeated: bool
    deprecated: bool
    optional: bool
    documentation: str
    union: Optional[str] = None


@dataclass
class HrzUnionField:
    """Represents a field that is part of a union within a message."""

    id: int
    name: str


@dataclass
class HrzUnion:
    """Represents a union discriminator field within a message."""

    name: str
    fields: List[HrzUnionField]


@dataclass
class HrzMessage(HrzTopLevelEntity):
    """Represents a protocol message with its fields and metadata."""

    fields: List[HrzField]
    unions: List[HrzUnion]
    is_path_root: bool
    is_path_leaf: bool
    path_root: Optional[str] = None
    path_root_type: Optional[str] = None
    path_root_type_is_enum: Optional[bool] = None


@dataclass
class HrzEnumValue:
    """Represents a single value within an enum."""

    name: str
    id: int
    deprecated: bool
    documentation: str
    label: str
    params_field_name: Optional[str] = None
    response_field_name: Optional[str] = None


@dataclass
class HrzEnum(HrzTopLevelEntity):
    """Represents an enum type with its values and metadata."""

    expose_to_style: bool
    values: List[HrzEnumValue]


@dataclass
class HrzMethod:
    """Represents a method within a service."""

    name: str
    input: str
    output: str
    documentation: str
    deprecated: bool
    id: int


@dataclass
class HrzService(HrzTopLevelEntity):
    """Represents a service with its methods and metadata."""

    id: int
    methods: List[HrzMethod]


@dataclass
class HrzFile:
    """Represents a protocol file with its dependencies."""

    name: str
    dependencies: List[str]


@dataclass
class HrzProtocol:
    """Represents the complete parsed protocol structure."""

    services: List[HrzService]
    enums: List[HrzEnum]
    messages: List[HrzMessage]
    files: List[HrzFile]
