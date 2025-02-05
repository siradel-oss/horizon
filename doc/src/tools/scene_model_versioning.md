# Scene model versioning

This documents explains the various mechanisms and tools that can be used to make sure scene dumps are forward compatible. An obvious example where this is useful is to migrate a tests suite to a newer version of the engine.

## Definitions

* **Scene model**: The scene model is the entire Protocol Buffers-defined interface of Horizon.
* **Descriptor set**: A descriptor set is the reflection data of Protocol Buffers definitions, in their serialized format.
* **Scene dump**: A scene dump is a dump of the entire scene model at a given time. It includes things like, camera position, layers definitions, settings, etc.
* **Scene model version**: A version of the scene model is a uniquely identifiable state of the scene model definition that has known previous and next states. A version is defined by its ID (a 32-bit integer, often written as a string of 8 hex digits), and a description. A version is created every time the scene model is modified in a way that makes it incompatible with the previous version using the standard Protocol Buffers compatibility mechanisms.
* **Scene model versions manifest**: This file (`//hrz/proto/history:versions_manifest.csv`) stores the list of all known scene model versions in chronological order. It contains a hash that can be used to check the consistency of the versions: namely that each version has indeed been created for its specific ancestor. This is important for migrations (see below).
* **Migration**: Migrations are short program designed to transform a scene dump from a specific version such that it is compatible with the next version.

## General principles

### Scene dump formats

- JSON: most user-friendly, however cannot be migrated because it doesn't use field numbers. It can still be imported through the web client, but all you can do is cross your fingers that it will work.
- Binary: not human readable, not copy-paste-able, but can be migrated, and more compact that JSON. Can be decoded into Protocol Buffer's text format using the `tools/scene_dump/encode_dump.py` and `tools/scene_dump/decode_dump.py` utilities.
- Base64: Same as binary, but encoded as base64. Mainly useful for copy-pasting.

Whenever possible, try to use the binary format.

### When to create a new version

A new version of the scene model should be created whenever it is modified in such a way that it would be incompatible with the previous version using the standard Protocol Buffers compatibility mechanisms. Namely, the following modifications break compatibility: (from the [Protocol Buffers documentation](https://developers.google.com/protocol-buffers/docs/proto3#updating))

- Changing the field number of an existing field or enum variant.
- Adding a field whose default value (0, null, empty list, empty string, etc.) wouldn't make sense.
    - For example adding an opacity property to the definition of 3D models: the default value, 0, would make the 3D models of older scenes invisible. So a migration is requires to make it a more acceptable value, like 1.
- Changing a field's type without changing its number. Exceptions:
    - int32, uint32, int64, uint64, and bool and compatible with each other.
    - sint32 and sint64 are compatible with each other, but not with other integer types.
    - string and bytes are compatible as long as the bytes are valid UTF-8.
    - Embedded messages are compatible with bytes if the bytes contain an encoded version of the message.
    - fixed32 is compatible with sfixed32, and fixed64 with sfixed64.
    - enum is compatible with int32, uint32, int64, and uint64 as long at the value in the numeric field is a valid enum variant.

If possible try to minimize the number of new versions created. At most one migration per branch/feature is a good rule of thumb. This can be achieved by using the "rebasing/merging" workflow to update the scenes already created on a branch, but the easiest way is still to try to modify the scene model as early as possible in the development cycle.

### When to update a version

Whenever possible (when compatibility is maintained) prefer updating the last scene model version rather than creating a new one. This mechanism simply overwrites the descriptor set of the last version with the current descriptor set.

### Migrations

Migrations from one version to the next happen as follow:

1. The version of the source dump is determined.
2. Two scene dump messages are created: one in the current version, one in the next.
3. The serialized data of the current version is parsed into *both* messages. This means that data that can be trivially transferred into the new version through Protocol Buffer's compatibility mechanisms is already migrated automatically.
4. The migration function uses the current message to migrate to the next message using the `DynamicMessage` interface that simplifies reflection.
5. This process repeats until the dump is in the latest version.

## Workflow

### Modifying the scene model

1. Modify the scene model as needed
2. If the modifications are compatible:
    1. Run the `tools/scene_model/update_latest_version.py` tool and you're done.
3. If the modifications are incompatible, and thus require a migration:
    1. Run the `tools/scene_model/new_version.py` tool.
    2. In the migration library (`//hrz/scene_dump:migration`) create a new migration function with the prototype given by the previous tool. This can be in any file linked to the library.
    3. Write the necessary migration code.
4. Run the `check-integrity` tool to check that everything is fine.

### Merging/rebasing

In order to maintain strict ordering of the versions, merging and rebasing require some manual steps.

- Let's assume you and your coworker created branches from master that has version A of the scene model.
- In your branch, you created version C, and thus the migration `A_to_C`. You have some scenes at version C.
- In their branch, your coworker created version B, and thus the migration `A_to_B`.
- Your coworker merges their branch on master. Now master is at version B. You branch is now not up to date:
    - Your version (C) should have now B as predecessor, but it as A.
    - Your scenes don't contain the migration `A_to_B`.
- Here is how to fix this:
    1. Resolve the merge conflicts in the manifest file by deleting the line of your version. This file should now be in the exact same state as master.
    1. Also reset the existing static scenes to the version from master if they diverged. They will be re-migrated to the new version later.
    2. Delete the descriptor set file corresponding to your version (`C.pbf`) from `//hrz/proto/history`.
    3. Create a new version we'll call D.
    4. Edit the `A_to_C` migration so that it is now `B_to_D` (both prototype and migration code).
    5. Set the version of your scenes that have version C to A (the last common version with B) using the `tools/scene_dump/set_dump_version.py` tool.
    6. Run the `tools/scene_dump/migrate_dump.py` tool to migrate them from A to B, then from B to D.
    7. If the scenes are still broken, tough luck! You have to re-create them.

### Editing a scene dump by hand

This can be useful for creating template scenes for instance. However editing binary is not super easy. Instead you can use the `tools/scene_dump/decode_dump.py` to turn it into a text format, edit it, and `tools/scene_dump/encode_dump.py` to do the reverse.

Note that if you're not using the latest version, you must specify it with the `-v` flag. In order to know the version of a binary dump, use the `tools/scene_dump/get_dump_version.py` tool.

### Migrating all static scenes

Don't do it. The scenes are automatically migrated when building the web client. If for some reason you need the migrated version of a scene, download it from the web client, or migrate a copy of it manually using the migration tool.

This is mostly to avoid merge conflicts.

## Tools

Those tools should be called from the root of the repository.

- `tools/scene_model/new_version.py <description>`
    - Create a new version with the given description. The description should describe in a few words what broke compatibility and required a migration.

* `tools/scene_model/update_latest_version.py`
    * Overwrites the last descriptor set with the current one. Use this only when you know they are compatible.
* `tools/scene_model/check_integrity.py`
    * Checks that the versions manifest file is in a correct state, and that the latest descriptor set is up to date.
* `tools/scene_dump/get_dump_version.py <bin dump file>`
    * Returns the 8-hex-digit version of the given scene dump.
* `tools/scene_dump/set_dump_version.py [-v version] <bin dump file>`
    * Sets the version of a given binary scene dump. If the version is not specified, uses latest.
* `tools/scene_dump/decode_dump.py [-v version] <input bin dump file> <output text file>`
    * Decodes a binary dump into its text form. If the version is not specified, uses latest.
* `tools/scene_dump/encode_dump.py [-v version] <input text file> <output bin dump file>`
    * Encode textual scene dump into its binary form. If the version is not specified, uses latest.
* `tools/scene_dump/migrate_dump.py <bin dump file>`
    * Migrates the given scene dump to the latest version.
* `tools/scene_model/get_latest_version.py`
    * Returns the 8-hex-digit of the latest version.
