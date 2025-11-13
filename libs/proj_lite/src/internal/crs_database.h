#pragma once

#include <stdint.h>
#include <stdlib.h>

typedef struct pl_CrsDictionaryEntry
{
    const char* string;
    uint16_t right_branch_offset;
} pl_CrsDictionaryEntry;

typedef struct pl_CrsDatabase
{
    pl_CrsDictionaryEntry* dict_entries;
    uint32_t dict_capacity;
    uint32_t dict_entry_count;
} pl_CrsDatabase;

typedef struct pl_CrsData
{
    const char* data;
    size_t size;
} pl_CrsData;

int pl_insert_dict_entry(
    pl_CrsDatabase* dict,
    uint16_t key,
    uint8_t key_length,
    const char* string);

pl_CrsData pl_get_crs_data(uint32_t queried_srid);
size_t pl_get_crs_string_length(
    const char* crs_ptr,
    size_t crs_data_size,
    const pl_CrsDatabase* dict);
