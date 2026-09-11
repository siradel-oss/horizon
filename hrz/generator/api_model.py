# SPDX-FileCopyrightText: Copyright 2026 Siradel
# SPDX-License-Identifier: MIT

from dataclasses import dataclass
from typing import Optional
from hrz.generator.protocol_model import HrzMessage, HrzProtocol, HrzUnion


@dataclass
class HrzApiClientMessage:
    name: str
    message: HrzMessage


@dataclass
class HrzApiPathTypeField:
    id: int
    name: str
    type: str
    is_primitive: bool
    is_enum: bool
    repeated: bool
    documentation: str
    union: Optional[str] = None


@dataclass
class HrzApiPathType:
    is_primitive: bool
    is_enum: bool
    is_path_leaf: bool
    name: str
    full_name: str
    file: str
    fields: list[HrzApiPathTypeField]
    unions: list[HrzUnion]
    documentation: str
    is_path_root: bool
    path_root: Optional[str] = None
    path_root_type: Optional[str] = None
    path_root_type_is_enum: Optional[bool] = None


@dataclass
class HrzApiModel:
    protocol: HrzProtocol
    client_messages: list[HrzApiClientMessage]
    to_forward_declare: list[str]
    enum_names: list[str]
    path_types: list[HrzApiPathType]
