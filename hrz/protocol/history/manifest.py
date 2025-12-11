from dataclasses import dataclass
from pathlib import Path
import csv
import hashlib
from typing import List


@dataclass
class ManifestEntry:
    id: str
    descriptor_hash: str
    chain_hash: str
    description: str


@dataclass
class Manifest:
    entries: List[ManifestEntry]

    def last_entry(self) -> ManifestEntry:
        return self.entries[-1]

    def write(self, path: Path):
        with open(path, "w+", newline="", encoding="utf8") as fp:
            writer = csv.writer(fp, quoting=csv.QUOTE_NONNUMERIC, lineterminator="\n")
            writer.writerows(
                [
                    [
                        entry.id,
                        entry.descriptor_hash,
                        entry.chain_hash,
                        entry.description,
                    ]
                    for entry in self.entries
                ]
            )

    def is_id_in(self, id: str) -> bool:
        return any(entry.id == id for entry in self.entries)

    def __len__(self):
        return len(self.entries)


def read_manifest(path: Path):
    entries = []
    with open(path, "r", encoding="utf8") as fp:
        reader = csv.reader(fp)
        for row in reader:
            entries.append(ManifestEntry(*row))
    return Manifest(entries)


def compute_hash(previous_hash: str, id: str, descriptor_hash: str):
    m = hashlib.sha3_256()
    m.update(previous_hash.encode("utf8"))
    m.update(id.encode("utf8"))
    m.update(descriptor_hash.encode("utf8"))
    return m.hexdigest()
