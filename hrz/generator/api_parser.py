from typing import Iterable, TypeVar
from hrz.generator.api_model import (
    HrzApiClientMessage,
    HrzApiModel,
    HrzApiPathType,
    HrzApiPathTypeField,
)
from hrz.generator.protocol_model import HrzProtocol
from hrz.generator.proto_types import *
from hrz.generator.protocol_model import HrzTopLevelEntity

GenericHrzTopLevelEntity = TypeVar("GenericHrzTopLevelEntity", bound=HrzTopLevelEntity)


def find_by_full_name(
    full_name, collection: Iterable[GenericHrzTopLevelEntity]
) -> GenericHrzTopLevelEntity | None:
    return next((item for item in collection if item.full_name == full_name), None)


def gather_path_types(
    protocol: HrzProtocol,
    enum_names: set[str],
    message_type: str,
    gathered_path_types: list[HrzApiPathType],
    to_forward_declare: list[str],
    visited_types: set[str] = set(),
    visit_stack: list[str] = [],
):
    if message_type in visit_stack:
        # Circular dependency
        if not message_type in to_forward_declare:
            to_forward_declare.append(message_type)
        return

    if message_type in visited_types:
        # Prevent infinite recursion
        return
    visited_types.add(message_type)

    enum_type = next((e for e in protocol.enums if e.full_name == message_type), None)
    msg_type = next((m for m in protocol.messages if m.full_name == message_type), None)

    if message_type in PB_PRIMITIVE_TYPES:
        primitive_type = HrzApiPathType(
            is_primitive=True,
            is_enum=False,
            is_path_leaf=True,
            name=message_type,
            full_name=message_type,
            file="hrz/protocol/types/wrappers",
            fields=[],
            documentation="",
            is_path_root=False,
        )
        gathered_path_types.append(primitive_type)
    elif enum_type != None:
        enum_type_dict = HrzApiPathType(
            is_primitive=False,
            is_enum=True,
            is_path_leaf=True,
            name=enum_type.name,
            full_name=enum_type.full_name,
            file=enum_type.file,
            fields=[],
            documentation=enum_type.documentation,
            is_path_root=False,
        )
        gathered_path_types.append(enum_type_dict)
    elif msg_type != None:
        if any(f for f in msg_type.fields if f.union is not None):
            raise Exception("Unions are not supported in paths (%s)" % message_type)

        msg_type_dict = HrzApiPathType(
            is_primitive=False,
            is_enum=False,
            is_path_leaf=msg_type.is_path_leaf,
            name=msg_type.name,
            full_name=msg_type.full_name,
            file=msg_type.file,
            fields=[
                HrzApiPathTypeField(
                    id=f.id,
                    name=f.name,
                    type=f.type,
                    is_primitive=f.type in PB_PRIMITIVE_TYPES,
                    is_enum=f.type in enum_names,
                    documentation=f.documentation,
                    repeated=f.repeated,
                )
                for f in msg_type.fields
            ],
            documentation=msg_type.documentation,
            is_path_root=msg_type.is_path_root,
            path_root=msg_type.path_root,
            path_root_type=msg_type.path_root_type,
            path_root_type_is_enum=msg_type.path_root_type_is_enum,
        )
        if not msg_type_dict.is_path_leaf:
            for f in msg_type_dict.fields:
                gather_path_types(
                    protocol,
                    enum_names,
                    f.type,
                    gathered_path_types,
                    to_forward_declare,
                    visited_types,
                    visit_stack + [message_type],
                )
        gathered_path_types.append(msg_type_dict)


def gather_client_messages(protocol: HrzProtocol) -> list[HrzApiClientMessage]:
    client_typed_message = find_by_full_name(
        "HrzProtocol.TypedMessage", protocol.messages
    )

    client_message_type_enum = find_by_full_name(
        "HrzProtocol.MessageType", protocol.enums
    )

    if client_typed_message is None or client_message_type_enum is None:
        raise Exception("Failed to find TypedMessage or MessageType in protocol")

    client_messages = []
    for enum_value in client_message_type_enum.values:
        for field in client_typed_message.fields:
            if field.name == enum_value.params_field_name:
                message = find_by_full_name(field.type, protocol.messages)
                if message is None:
                    raise Exception(
                        "Failed to find message for client message type %s"
                        % enum_value.name
                    )
                client_messages.append(
                    HrzApiClientMessage(
                        enum_value=enum_value,
                        message=message,
                    )
                )
                break

    return client_messages


def parse(protocol: HrzProtocol) -> HrzApiModel:
    root_types = {
        msg.full_name: msg.path_root
        for msg in protocol.messages
        if msg.is_path_root and msg.path_root is not None
    }
    path_types = []
    to_forward_declare = []

    enum_names = {e.full_name for e in protocol.enums}

    for root_type in root_types:
        gather_path_types(
            protocol, enum_names, root_type, path_types, to_forward_declare
        )

    return HrzApiModel(
        protocol=protocol,
        client_messages=gather_client_messages(protocol),
        to_forward_declare=to_forward_declare,
        enum_names=list(enum_names),
        path_types=path_types,
    )
