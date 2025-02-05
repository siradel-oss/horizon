import sys
import heapq
import sqlite3
import argparse

def split(string, separators):
    part = ""
    for character in string:
        part = part + character
        if character in separators:
            yield part
            part = ""
    if part != "":
        yield part

def dict_compress(projections, separators):
    frequencies = dict()

    for projection in projections:
        for part in split(projection[1], separators):
            frequencies[part] = frequencies.get(part, 0) + 1

    htree = []
    keys = dict()
    key_tree_size = 0

    for string, freq in frequencies.items():
        heapq.heappush(htree, (freq, [string]))
        key_tree_size += 1

    while len(htree) > 1:
        left_node = heapq.heappop(htree)
        right_node = heapq.heappop(htree)

        for string in left_node[1]:
            # Add a 0 to the encoding of all nodes on the left
            keys[string] = "0" + keys.get(string, "")

        for string in right_node[1]:
            # Add a 1 to the encoding of all nodes on the right
            keys[string] = "1" + keys.get(string, "")

        # Join both as one node and push to tree
        heapq.heappush(htree, (left_node[0] + right_node[0], left_node[1] + right_node[1]))

        key_tree_size += 1

    padding_key = ""
    longest_key_length = 0
    for _, key in keys.items():
        if len(key) >= longest_key_length:
            padding_key = key
            longest_key_length = len(key)
    assert(len(padding_key) >= 8)

    projection_lengths = []
    encoded_lines = ""

    for projection in projections:
        srid = projection[0]
        proj_string = projection[1]

        encoded_line = ""
        for part in split(proj_string, separators):
            encoded_line += keys[part]
        missing_bytes = len(encoded_line) % 8
        if missing_bytes != 0:
            missing_bytes = 8 - missing_bytes
        encoded_line += padding_key[:missing_bytes]
        encoded_lines += encoded_line

        assert(len(encoded_line) % 8 == 0)

        projection_lengths.append((srid, int(len(encoded_line) / 8)))

    encoded_bytes = []

    for i in range(0, len(encoded_lines), 8):
        b = 0
        for j in range(8):
            b += (1 if encoded_lines[i + j] == "1" else 0) << j
        encoded_bytes.append(b)

    return projection_lengths, encoded_bytes, keys, key_tree_size

# Because 'to_bytes()' does not exist on Python 2.
# From https://stackoverflow.com/a/20793663
def to_bytes(n, length, endianess = "big"):
    if sys.version_info[0] >= 3:
        return n.to_bytes(length, endianess)
    else:
        s = "{:0{}x}".format(n, length * 2).decode("hex")
        return s if endianess == "big" else s[::-1]

def compress_db(db_file_path, output_file_path, verbose):
    projections = []

    conn = sqlite3.connect(db_file_path)
    resp = conn.execute("SELECT auth, code, proj_string FROM crs")
    for row in resp:
        auth = row[0]
        code = int(row[1])
        proj_str = row[2]
        if auth == "EPSG":
            projections.append((code, proj_str))
        else:
            print("Warning: Non-EPSG projections are not currently supported, skipping %s:%s" % (auth, code))

    projection_lengths, proj_bytes, keys, key_tree_size = dict_compress(projections, [" ", "="])

    assert(len(keys) <= 256 * 256)

    key_list = []
    for string, key in keys.items():
        key_list.append((key, string))

    key_list.sort(key = lambda entry: entry[0])

    if verbose:
        print("%s keys (tree entries: %s)" % (len(key_list), key_tree_size))

    dict_bytes = []
    for key, string in key_list:
        int_key = 0
        for i in range(len(key)):
            int_key += (1 if key[i] == "1" else 0) << i
        dict_bytes += to_bytes(len(key), 1, "little")
        dict_bytes += to_bytes(int_key, 2, "little")
        dict_bytes += bytes(string.encode("utf-8"))
        dict_bytes += [0] # make a 0-terminated string

    lengths_bytes = []
    for srid, length in projection_lengths:
        lengths_bytes += to_bytes(srid, 2, "little")
        lengths_bytes += to_bytes(length, 1, "little")

    dict_size = len(dict_bytes)
    lengths_size = len(lengths_bytes)
    proj_size = len(proj_bytes)
    total_size = 4 + 4 + 4 + dict_size + 4 + lengths_size + 4 + proj_size

    output_bytes = []
    output_bytes += to_bytes(total_size, 4, "little")
    output_bytes += to_bytes(dict_size, 4, "little")
    output_bytes += to_bytes(key_tree_size, 4, "little")
    output_bytes += dict_bytes
    output_bytes += to_bytes(lengths_size, 4, "little")
    output_bytes += lengths_bytes
    output_bytes += to_bytes(proj_size, 4, "little")
    output_bytes += proj_bytes

    if verbose:
        print("Dictionary size:  %s bytes" % (dict_size))
        print("Lengths size:     %s bytes" % (lengths_size))
        print("Projections size: %s bytes" % (proj_size))
        print("Total size:       %s bytes" % (total_size))

    with open(output_file_path, "wb") as out_file:
        out_file.write(bytearray(output_bytes))

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Generate a compressed projection database.")
    parser.add_argument("db_file_path", help="Path to the input database file")
    parser.add_argument("output_file_path", help="Path to the output compressed database file")
    parser.add_argument("-v", "--verbose", action='store_true', help="Enable verbose mode (default: false)")
    args = parser.parse_args()

    compress_db(args.db_file_path, args.output_file_path, args.verbose)
