#pragma once

#include "hrz/fnd/arena.h"
#include "hrz/fnd/static_vector.h"
#include "hrz/fnd/variant.h"

#include <lin_maths.h>

namespace hrz
{

// This implementation of a kdtree stores 2D quads and is used to lookup intersections. It is
// implemented as a 4D kdtree with point coordinates being (x_min, x_max, y_min, y_max). Unlike
// traditional kdtrees, all points are in the leaves, and each leaf can have multiple points. They
// are subdivided along the axes that has the maximum extent when a node has too many points.
// Overlap queries are done as follows:
//
// For a query rectangle Q, a rectangle R does not overlap Q if:
//    Q(x_max) < R(x_min) or Q(x_min) > R(x_max)
// or Q(y_max) < R(y_min) or Q(y_min) > R(y_max)
//
// Hence they do overlap if:
//     Q(x_max) >= R(x_min) and Q(x_min) <= R(x_max)
// and Q(y_max) >= R(y_min) and Q(y_min) <= R(y_max)
//
// This means that the range query we have to do in the quadtree to respect this expression is:
// (
//     [    -inf; Q(x_max)],
//     [Q(x_min);     +inf],
//     [    -inf; Q(y_max)],
//     [Q(y_min);     +inf]
// )

class Kdtree
{
    static constexpr size_t MAX_LEAF_SIZE = 16;

    static constexpr int XMIN = 0;
    static constexpr int XMAX = 1;
    static constexpr int YMIN = 2;
    static constexpr int YMAX = 3;

    struct Node;

    struct Leaf
    {
        lm::vec4 min{std::numeric_limits<float>::max()};
        lm::vec4 max{std::numeric_limits<float>::lowest()};
        hrz::StaticVector<lm::vec4, MAX_LEAF_SIZE> rects;

        void insert(lm::vec4 pt)
        {
            min = lm::min(min, pt);
            max = lm::max(max, pt);
            rects.push_back(pt);
        }
    };

    struct SplitNode
    {
        int split_axis;
        float split_value;
        Node* children[2];
    };

    using MyVariant = std::variant<Leaf, SplitNode>;

    struct Node : public MyVariant
    {
        template<typename... Args>
        Node(std::in_place_t, Args&&... args) : MyVariant(std::forward<Args>(args)...)
        {
        }
    };

    static constexpr int LEAF = hrz::index_of_variant<MyVariant, Leaf>();
    static constexpr int SPLIT_NODE = hrz::index_of_variant<MyVariant, SplitNode>();

    Arena _arena;
    Node* _root = nullptr;

    void split_leaf(Node* node)
    {
        assert(node->index() == LEAF);
        Leaf children[2];

        Leaf& leaf = std::get<Leaf>(*node);
        assert(leaf.rects.size() >= 2);

        // Look for the axis that has the max spread amongst the elements.
        lm::vec4 diff = leaf.max - leaf.min;
        float max_extent = 0;
        int max_extent_axis = 0;
        for (int i = 0; i < 4; ++i)
        {
            if (diff.m[i] > max_extent)
            {
                max_extent = diff.m[i];
                max_extent_axis = i;
            }
        }

        // If the max extent is 0, all points are the same in this leaf.
        // Instead of splitting it, we just reduce its side to 1 since all elements are the
        // same. This means that we can just continue inserting in it afterwards.
        if (max_extent == 0)
        {
            leaf.rects.set_size(1);
            return;
        }

        // Compute the median along the chosen axis
        hrz::StaticVector<float, MAX_LEAF_SIZE> sorted_axis_values;
        for (const auto& pt : leaf.rects)
        {
            sorted_axis_values.push_back(pt.m[max_extent_axis]);
        }

        std::ranges::sort(sorted_axis_values);
        const float median = sorted_axis_values[sorted_axis_values.size() / 2];

        for (const auto& pt : leaf.rects)
        {
            const int child = (pt.m[max_extent_axis] < median) ? 0 : 1;
            children[child].insert(pt);
        }

        SplitNode split_node{};
        split_node.split_value = median;
        split_node.split_axis = max_extent_axis;
        split_node.children[0] = _arena.alloc<Node>(std::in_place, std::move(children[0]));
        split_node.children[1] = _arena.alloc<Node>(std::in_place, std::move(children[1]));
        *node = Node(std::in_place, std::move(split_node));
    }

    void insert_in_leaf(Node* node, lm::vec4 pt)
    {
        assert(node->index() == LEAF);
        Leaf& leaf = std::get<Leaf>(*node);
        if (leaf.rects.size() < MAX_LEAF_SIZE)
        {
            leaf.insert(pt);
        }
        else
        {
            split_leaf(node);
            try_insert(node, pt);
        }
    }

    void try_insert(Node* node, lm::vec4 pt)
    {
        if (node->index() == SPLIT_NODE)
        {
            auto& split_node = std::get<SplitNode>(*node);
            int child_index = (pt.m[split_node.split_axis] < split_node.split_value) ? 0 : 1;
            try_insert(split_node.children[child_index], pt);
        }
        else
        {
            assert(node->index() == LEAF);
            insert_in_leaf(node, pt);
        }
    }

    bool intersects(Node* node, lm::vec4 q)
    {
        if (node->index() == SPLIT_NODE)
        {
            auto& split = std::get<SplitNode>(*node);
            bool visit0 = false;
            bool visit1 = false;
            switch (split.split_axis)
            {
                case XMIN:
                {
                    visit0 = true;
                    visit1 = q.m[XMAX] >= split.split_value;
                    break;
                }
                case XMAX:
                {
                    visit0 = q.m[XMIN] <= split.split_value;
                    visit1 = true;
                    break;
                }
                case YMIN:
                {
                    visit0 = true;
                    visit1 = q.m[YMAX] >= split.split_value;
                    break;
                }
                case YMAX:
                {
                    visit0 = q.m[YMIN] <= split.split_value;
                    visit1 = true;
                    break;
                }
                default: break;
            }
            if (visit0 && intersects(split.children[0], q)) return true;
            if (visit1 && intersects(split.children[1], q)) return true;
        }
        else
        {
            auto& leaf = std::get<Leaf>(*node);
            for (const auto& r : leaf.rects)
            {
                if (q.m[XMAX] >= r.m[XMIN] && q.m[XMIN] <= r.m[XMAX] && q.m[YMAX] >= r.m[YMIN]
                    && q.m[YMIN] <= r.m[YMAX])
                {
                    return true;
                }
            }
        }
        return false;
    }

public:
    Kdtree() { _root = _arena.alloc<Node>(std::in_place, Leaf{}); }

    Kdtree(const Kdtree&) = delete;
    Kdtree& operator =(const Kdtree&) = delete;

    Kdtree(Kdtree&&) = delete;
    Kdtree& operator =(Kdtree&&) = delete;

    void insert(const lm::bbox2 box)
    {
        lm::vec4 v{box.min.x, box.max.x, box.min.y, box.max.y};
        try_insert(_root, v);
    }

    bool intersects(const lm::bbox2 box)
    {
        lm::vec4 v{box.min.x, box.max.x, box.min.y, box.max.y};
        return intersects(_root, v);
    }
};

} // namespace hrz
