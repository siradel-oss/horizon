#pragma once

#include "hrz/fnd/log.h"

#include <assert.h>

#include <vector>

namespace hrz
{
/**
 * Pool that recycles indices by appending them with a generation index.
 * Useful for checking lifetime of internally managed objects from the
 * outside of a system.
 * Generation eventually wraps around.
 * BackingType should be an unsigned integer type.
 *
 * Example: BackingType = uint16_t, GenWidth = 4, IndexWidth = 8
 * 0000 GGGG IIII IIII
 */

template<typename BackingType, int GenWidth, int IndexWidth>
class GenIndexPool
{
    static_assert(sizeof(BackingType) * 8 >= (GenWidth + IndexWidth), "Backing type size");
    static_assert(GenWidth >= 1, "Generation needs at least 1 bit");
    static_assert(IndexWidth >= 1, "Index needs at least 1 bit");

    enum : BackingType
    {
        GenerationMask = (((BackingType)1 << GenWidth) - 1) << IndexWidth,
        GenerationShift = IndexWidth,
        IndexMask = ((BackingType)1 << IndexWidth) - 1,

        IndexMax = ((BackingType)1 << IndexWidth) - 1,
        GenerationMax = ((BackingType)1 << GenWidth) - 1
    };

    inline static BackingType combine(BackingType generation, BackingType index)
    {
        return ((generation << GenerationShift) & GenerationMask) | (index & IndexMask);
    }

    inline static BackingType get_generation(BackingType handle)
    {
        return (handle & GenerationMask) >> GenerationShift;
    }

public:
    using Handle = BackingType;

    inline static BackingType get_index(BackingType handle) { return handle & IndexMask; }

    Handle alloc()
    {
        if (!_free_indices.empty())
        {
            BackingType index = _free_indices.back();
            _free_indices.pop_back();

            BackingType generation = _generations[index];

            return combine(generation, index);
        }
        else
        {
            BackingType index = (BackingType)_generations.size();
            if (index > IndexMax)
            {
                HRZ_LOG_ERROR("Exceeded index pool capacity");
                assert(false && "Exceeded index pool capacity");
            }
            _generations.push_back(1);
            return combine(1, index);
        }
    }

    void release(Handle h)
    {
        if (!is_valid(h)) return;

        BackingType index = get_index(h);
        _free_indices.push_back(index);

        BackingType& generation = _generations[index];
        if (generation == GenerationMax)
        {
            generation = 1;
        }
        else
        {
            generation += 1;
        }
    }

    inline bool is_valid(Handle h) const
    {
        BackingType index = get_index(h);
        if (index >= _generations.size()) return false;

        return _generations[index] == get_generation(h);
    }

private:
    std::vector<BackingType> _generations;
    std::vector<BackingType> _free_indices;
};

} // namespace hrz
