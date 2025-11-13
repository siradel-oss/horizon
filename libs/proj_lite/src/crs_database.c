#include "internal/crs_database.h"

#include "proj_lite.h"

#include <assert.h>
#include <string.h>

#include <stddef.h>

/*
 * CRS data blob format:
 *
 * Every multiple-byte value is serialised in little-endian format.
 *
 * Header:
 *      blob size (in bytes)        integer                 4 bytes
 *
 * Dictionary section:
 *      section size (in bytes)     integer                 4 bytes
 *      key tree entry count        integer                 4 bytes
 *   repeated, and sorted by key:
 *      key length (in bits)        integer                 1 byte
 *      key                         integer, left-padded    2 bytes
 *      sub-string                  null-terminated string  n bytes
 *
 * CRS sizes section:
 *      section size (in bytes)     integer                 4 bytes
 *    repeated:
 *      SRID                        integer                 2 bytes
 *      compressed size (in bytes)  integer                 1 byte
 *
 * Compressed data section:
 *      compressed data size        integer                 4 bytes
 *      compressed data             raw data                n bytes
 *
 *
 * CRS proj strings are compressed using Huffman coding. They are split in substrings,
 * and each substring is encoded with a key. The compressed string is made of the
 * concatenation of the keys.
 *
 * The dictionary contains the key-to-substring mappings. The keys are ordered in a
 * way that makes building a binary tree (used to look them up later) in a contiguous
 * array simple. The "key tree entry count" field gives the total number of entries
 * in the binary tree.
 *
 * Because keys can be composed of any number of bits, and because any bit pattern
 * could be a valid key, it is necessary to specify the bit-length of each key.
 * The bit are ordered in the order in which the branches in the binary tree must be
 * traversed, from the lowest weight bit to the heighest weight bit. The remaining
 * bits are not coding for any information.
 *
 * For example, for the key "101101011100". The branches to take in the tree are
 * right, left, right, right, left, etc. The serialisation is:
 *
 *              low byte| hi byte
 *              --------|--------
 *              10101101|xxxx0011
 *
 * The declared key length is 12.
 *
 * Each CRS is identified by its EPSG SRID. (EPSG is the only supported authority at
 * the moment.) Each CRS has an entry in the sizes section. This section associates
 * SRIDs with the size of the compressed representation of their PROJ string. They
 * are ordered just as in the compressed data section. This means that by iterating
 * over the sizes section, the starting position of each compressed string can be
 * computed, by adding the sizes, starting with the compressed data section address.
 *
 *
 * How to decompress a PROJ string:
 *   * Iterate over the sizes section (while adding the sizes).
 *   * Once the queried SRID is found, start reading the compressed data at the
 *     offset that was obtained by adding the sizes.
 *   * Read each bit, until the whole compressed string has been read.
 *   * Go down the binary tree of the dictionary, for each bit.
 *   * If a substring is associated with the current entry in the dictionary,
 *     append that string to the decompressed string.
 *   * If there are left-overs bits at the end, discard them. (They are needed
 *     for the compressed string boundaries to fall on whole bytes, and are
 *     chosen in order not to code for any value.)
 */

extern const size_t _embedded_resource_crs_db_data_len;
extern const char _embedded_resource_crs_db_data[];

static inline uint8_t read_u8(const char* ptr)
{
    uint8_t v;
    memcpy(&v, ptr, 1);
    return v;
}

static inline uint16_t read_u16(const char* ptr)
{
    uint16_t v;
    memcpy(&v, ptr, 2);
    return v;
}

static inline uint32_t read_u32(const char* ptr)
{
    uint32_t v;
    memcpy(&v, ptr, 4);
    return v;
}

pl_CrsDatabase* pl_load_crs_database()
{
    uint32_t total_size = read_u32(_embedded_resource_crs_db_data);
    assert(total_size == _embedded_resource_crs_db_data_len);
    (void)total_size; // Prevent "unused variable" warning when assertions are disabled.

    const char* dictionary_data = _embedded_resource_crs_db_data + sizeof(uint32_t);
    uint32_t dictionary_size = read_u32(dictionary_data);

    const char* sizes_data = dictionary_data + sizeof(uint32_t) * 2 + dictionary_size;

    uint32_t key_tree_entry_count = read_u32(dictionary_data + sizeof(uint32_t));

    pl_CrsDictionaryEntry* dict_entries =
        (pl_CrsDictionaryEntry*)calloc(key_tree_entry_count, sizeof(pl_CrsDictionaryEntry));
    dict_entries[0].string = 0;
    dict_entries[0].right_branch_offset = 0;

    pl_CrsDatabase* db = (pl_CrsDatabase*)malloc(sizeof(pl_CrsDatabase));
    db->dict_entries = dict_entries;
    db->dict_capacity = key_tree_entry_count;
    db->dict_entry_count = 1;

    const char* dict_entry = dictionary_data + sizeof(uint32_t) * 2;
    while (dict_entry < sizes_data)
    {
        uint8_t key_length = read_u8(dict_entry);
        uint16_t key = read_u16(dict_entry + sizeof(uint8_t));
        const char* string = dict_entry + sizeof(uint8_t) + sizeof(uint16_t);
        size_t string_length = strlen(string);

        int res = pl_insert_dict_entry(db, key, key_length, string);
        if (res != 0)
        {
            pl_destroy_crs_database(db);
            return 0;
        }

        dict_entry += sizeof(uint8_t) + sizeof(uint16_t) + string_length + sizeof(uint8_t);
    }

    return db;
}

void pl_destroy_crs_database(pl_CrsDatabase* db)
{
    if (db != 0) free(db->dict_entries);

    free(db);
}

int pl_insert_dict_entry(pl_CrsDatabase* db, uint16_t key, uint8_t key_length, const char* string)
{
    uint32_t entry_index = 0;

    for (unsigned int b = 0; b < key_length; ++b)
    {
        uint16_t bit = key & (1 << b);

        if (bit == 0)
        {
            entry_index = entry_index + 1;
        }
        else
        {
            uint16_t offset = db->dict_entries[entry_index].right_branch_offset;
            if (offset == 0)
            {
                offset = (uint16_t)(db->dict_entry_count - entry_index);
                db->dict_entries[entry_index].right_branch_offset = offset;
            }

            entry_index += offset;
        }

        if (entry_index >= db->dict_capacity)
        {
            // Dictionary is full!
            // This should not happen if the data blob has been properly generated.
            return -1;
        }

        if (db->dict_entry_count <= entry_index)
        {
            db->dict_entry_count = entry_index + 1;
        }

        if (b == key_length - 1)
        {
            pl_CrsDictionaryEntry* entry = db->dict_entries + entry_index;
            entry->string = string;
        }
    }

    return 0;
}

pl_CrsData pl_get_crs_data(uint32_t srid)
{
    pl_CrsData data;
    data.data = 0;
    data.size = 0;

    const char* dictionary_data = _embedded_resource_crs_db_data + sizeof(uint32_t);
    uint32_t dictionary_size = read_u32(dictionary_data);

    const char* sizes_data = dictionary_data + sizeof(uint32_t) * 2 + dictionary_size;
    uint32_t sizes_size = read_u32(sizes_data);

    const char* compressed_data = sizes_data + sizeof(uint32_t) + sizes_size;

    const char* read_pos = sizes_data + sizeof(uint32_t);
    const char* this_crs_data = compressed_data + sizeof(uint32_t);
    while (read_pos < compressed_data)
    {
        uint16_t this_srid = read_u16(read_pos);
        uint8_t this_srid_size = read_u8(read_pos + sizeof(uint16_t));

        if (this_srid == srid)
        {
            data.data = this_crs_data;
            data.size = this_srid_size;
            return data;
        }

        this_crs_data += this_srid_size;

        read_pos += sizeof(uint16_t) + sizeof(uint8_t);
    }

    return data;
}

size_t pl_get_crs_string_length(
    const char* crs_data,
    size_t crs_data_size,
    const pl_CrsDatabase* dict)
{
    const char* crs_data_end = crs_data + crs_data_size;

    uint32_t dict_entry_index = 0;

    size_t length = 0;

    while (crs_data < crs_data_end)
    {
        uint8_t byte = read_u8(crs_data);

        for (unsigned int b = 0; b < 8; ++b)
        {
            uint8_t bit = byte & (1 << b);

            if (bit == 0)
            {
                dict_entry_index += 1;
            }
            else
            {
                dict_entry_index += dict->dict_entries[dict_entry_index].right_branch_offset;
            }

            const char* string = dict->dict_entries[dict_entry_index].string;
            if (string != 0)
            {
                length += strlen(string);

                dict_entry_index = 0;
            }
        }

        crs_data += 1;
    }

    return length;
}

static int pl_decompress_crs(
    const char* crs_data,
    size_t crs_data_size,
    const pl_CrsDatabase* dict,
    char* dest,
    size_t dest_size)
{
    if (dest_size < 1) return -2;

    const char* crs_data_end = crs_data + crs_data_size;
    const char* dest_end = dest + dest_size;

    uint32_t dict_entry_index = 0;

    dest[0] = 0;
    char* write_pos = dest;

    while (crs_data < crs_data_end)
    {
        uint8_t byte = read_u8(crs_data);

        for (unsigned int b = 0; b < 8; ++b)
        {
            uint8_t bit = byte & (1 << b);

            if (bit == 0)
            {
                dict_entry_index += 1;
            }
            else
            {
                dict_entry_index += dict->dict_entries[dict_entry_index].right_branch_offset;
            }

            const char* string = dict->dict_entries[dict_entry_index].string;
            if (string != 0)
            {
                // Copy the substring into the destination buffer.
                // There is not readily available multi-platform function
                // that copies string while also checking the bounds of the buffers,
                // so it is written explicitely.

                const char* read_pos = string;
                while (write_pos < dest_end)
                {
                    char c = *read_pos++;
                    *write_pos++ = c;
                    if (c == '\0') break;
                }

                // @Safety dst_size >= 1, so dest < dest_end, hence if write_pos == dst_end,
                // write_pos - 1 >= dest, which is readable.
                if (write_pos == dest_end && *(write_pos - 1) != 0)
                {
                    // The copied string is truncated because the destination
                    // buffer is too small.
                    // Ensure that the truncated string is still null-terminated.
                    *(write_pos - 1) = 0;
                    return -1;
                }

                write_pos--;

                assert(*write_pos == 0);

                dict_entry_index = 0;
            }
        }

        crs_data += 1;
    }

    return (int)(write_pos - dest);
}

pl_CrsDatabaseResult pl_get_crs(
    const pl_CrsDatabase* db,
    const char* authority,
    unsigned int srid,
    pl_Crs* out_crs)
{
    size_t crs_str_length;
    pl_CrsDatabaseResult res1 = pl_get_crs_proj_str_length(db, authority, srid, &crs_str_length);

    if (res1 != pl_CrsDatabaseResult_Ok)
    {
        return res1;
    }

    char* str = (char*)calloc(crs_str_length + 1, sizeof(char));

    res1 = pl_get_crs_proj_str(db, authority, srid, str, &crs_str_length, crs_str_length + 1);
    assert(res1 == pl_CrsDatabaseResult_Ok);

    pl_Result res2 = pl_crs_from_proj_str(str, crs_str_length, out_crs);

    free(str);

    if (res2 == pl_Result_Ok)
    {
        return pl_CrsDatabaseResult_Ok;
    }

    return pl_CrsDatabaseResult_UnsupportedCrs;
}

pl_CrsDatabaseResult pl_get_crs_proj_str_length(
    const pl_CrsDatabase* db,
    const char* authority,
    unsigned int srid,
    size_t* out_length)
{
    if (strcmp(authority, "EPSG") != 0)
    {
        return pl_CrsDatabaseResult_CrsNotFound;
    }

    pl_CrsData crs_data = pl_get_crs_data(srid);

    if (crs_data.data != 0)
    {
        *out_length = pl_get_crs_string_length(crs_data.data, crs_data.size, db);
        return pl_CrsDatabaseResult_Ok;
    }

    return pl_CrsDatabaseResult_CrsNotFound;
}

pl_CrsDatabaseResult pl_get_crs_proj_str(
    const pl_CrsDatabase* db,
    const char* authority,
    unsigned int srid,
    char* out_proj_str,
    size_t* out_proj_str_length,
    size_t proj_str_buffer_size)
{
    if (strcmp(authority, "EPSG") != 0)
    {
        return pl_CrsDatabaseResult_CrsNotFound;
    }

    pl_CrsData crs_data = pl_get_crs_data(srid);

    if (crs_data.data != 0)
    {
        int res =
            pl_decompress_crs(crs_data.data, crs_data.size, db, out_proj_str, proj_str_buffer_size);
        if (res > 0)
        {
            *out_proj_str_length = (size_t)res;
            return pl_CrsDatabaseResult_Ok;
        }

        *out_proj_str_length = proj_str_buffer_size - 1;
        return pl_CrsDatabaseResult_BufferTooSmall;
    }

    return pl_CrsDatabaseResult_CrsNotFound;
}
