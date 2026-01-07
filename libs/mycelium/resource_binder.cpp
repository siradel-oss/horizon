#include "mycelium/renderer.h"

#include <assert.h>

#include <vector>

namespace my
{
class ResourceBinderImpl : public ResourceBinder
{
    template<typename T, int N>
    struct WorkingState
    {
        static_assert(N < 64, "Too many binding points!");

        T states[N];
        uint64_t assigned = 0;
        int8_t assigned_count = 0;
        int8_t id_to_index[N];

        constexpr bool is_assigned(int i) const { return (assigned & ((uint64_t)1 << i)) != 0; }

        void assign(int id, const T& value)
        {
            if (id < 0 || id >= N)
            {
                assert(!"Bind point out of bounds.");
                return;
            }

            if (!is_assigned(id))
            {
                id_to_index[id] = assigned_count++;
                assigned |= (uint64_t)1 << id;
            }

            states[id_to_index[id]] = value;
        }
    };

    using UniformState = WorkingState<UboBinding, MaxUniformBlocks>;
    using TextureState = WorkingState<TextureBinding, MaxTextureUnits>;

    std::vector<UniformState> _ubo_stack;
    std::vector<TextureState> _texture_stack;

public:
    ResourceBinderImpl()
    {
        _ubo_stack.emplace_back();
        _texture_stack.emplace_back();
    }

    void push_state() override
    {
        _ubo_stack.resize(_ubo_stack.size() + 1);
        _ubo_stack.back() = _ubo_stack[_ubo_stack.size() - 2];

        _texture_stack.resize(_texture_stack.size() + 1);
        _texture_stack.back() = _texture_stack[_texture_stack.size() - 2];
    }

    void pop_state() override
    {
        if (_ubo_stack.size() < 1) return;

        _ubo_stack.pop_back();
        _texture_stack.pop_back();
    }

    void bind(std::span<const UboBinding> bindings) override
    {
        for (const auto& binding : bindings)
        {
            _ubo_stack.back().assign(binding.index, binding);
        }
    }

    void bind(std::span<const TextureBinding> bindings) override
    {
        for (const auto& binding : bindings)
        {
            _texture_stack.back().assign(binding.index, binding);
        }
    }

    State get_current_state() override
    {
        return State{
            std::span<const UboBinding>(
                _ubo_stack.back().states, (size_t)_ubo_stack.back().assigned_count),
            std::span<const TextureBinding>(
                _texture_stack.back().states, (size_t)_texture_stack.back().assigned_count)};
    }
};

ResourceBinder* ResourceBinder::create()
{
    return new ResourceBinderImpl();
}

} // namespace my
