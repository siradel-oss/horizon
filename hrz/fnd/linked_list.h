// SPDX-FileCopyrightText: Copyright 2023 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <type_traits>

namespace hrz
{

// This is an intrusive doubly-linked list with externally-managed nodes memory.
// The list is circular and delimited by a sentinel.
// I suggest using hrz::ObjectPool for managing the nodes.
// Nodes should have T* prev and T* next as their first and second fields.
template<typename T>
struct LinkedList
{
    struct SentinelNode
    {
        T* prev{};
        T* next{};
        alignas(T) std::byte _dummy[sizeof(T)];
    };

    // SentinelNode must have its layout compatible with T for the next and prev pointers.
    // It is necessary because if we use T has the type for the sentinel node, we need to initialize
    // the value, and T might not have a default constructor.
    // It's important that we never access the fields of T instead of next and prev through the
    // pointer to the sentinel node of type T* otherwise we'll read uninitialized memory..

    static_assert(sizeof(SentinelNode) >= sizeof(T), "Sentinel node layout");
    static_assert(alignof(SentinelNode) == alignof(T), "Sentinel node layout");
    static_assert(offsetof(SentinelNode, prev) == offsetof(T, prev), "Sentinel node layout");
    static_assert(offsetof(SentinelNode, next) == offsetof(T, next), "Sentinel node layout");
    static_assert(std::is_same_v<decltype(T::prev), T*>, "prev type must be T*");
    static_assert(std::is_same_v<decltype(T::next), T*>, "next type must be T*");

    SentinelNode _sentinel_fake;
    T* _sentinel;

    LinkedList() : _sentinel_fake{}, _sentinel{(T*)&_sentinel_fake}
    {
        _sentinel->prev = _sentinel;
        _sentinel->next = _sentinel;
    }

    // Implementing those isn't hard, but we don't need them for now.
    LinkedList(const LinkedList&) = delete;
    LinkedList(LinkedList&&) = delete;
    LinkedList& operator =(const LinkedList&) = delete;
    LinkedList& operator =(LinkedList&&) = delete;

    void insert_after(T* in_list, T* to_insert)
    {
        to_insert->next = in_list->next;
        to_insert->prev = in_list;
        in_list->next->prev = to_insert;
        in_list->next = to_insert;
    }

    void detach(T* node)
    {
        node->prev->next = node->next;
        node->next->prev = node->prev;
        node->prev = nullptr;
        node->next = nullptr;
    }

    void insert_head(T* to_insert) { insert_after(_sentinel, to_insert); }

    void insert_tail(T* to_insert) { insert_after(tail(), to_insert); }

    constexpr bool empty() const { return _sentinel->next == _sentinel; }

    T* head() { return _sentinel->next; }

    const T* head() const { return _sentinel->next; }

    T* tail() { return _sentinel->prev; }

    const T* tail() const { return _sentinel->prev; }

    constexpr bool is_valid(T* node) const { return node != _sentinel; }

    constexpr bool is_tail(T* node) const { return node->next == _sentinel; }

    // It is tempting to implement an iterator for linked list. I tried that and got bitten so hard
    // that I promptly sent it into oblivion. The issue was that the more common way to write
    // for-range loops is something like for(auto it: list), and because there is auto, it becomes a
    // copy of a value. A common thing we do with linked lists is take the address of a node to
    // (un)link it. But using &it would be wrong here. Of course the correct way of writing this
    // could be with a reference, or using an address function that returns node->prev->next. But,
    // instead of having to be very careful every time we iterate over linked lists, I prefer just
    // not implementing a dangerous primitive.
    //  -slerouzic, 2023-11-09
};

} // namespace hrz
