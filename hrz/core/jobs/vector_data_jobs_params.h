// SPDX-FileCopyrightText: Copyright 2025 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "hrz/common/blob_allocator.h"
#include "hrz/common/blob_array.h"
#include "hrz/common/geo.h"
#include "hrz/common/tile_coords.h"
#include "hrz/common/vector_data/feature_ids.h"
#include "hrz/common/vector_data/packed_attribute_values.h"
#include "hrz/common/vector_data/tile.h"
#include "hrz/common/vector_data/tile_geometry.h"
#include "hrz/protocol/attributes/feature_id.pb.h"
#include "hrz/protocol/attributes/transform.pb.h"
#include "hrz/protocol/client_data/vector_data_request_response.pb.h"
#include "hrz/protocol/vector/data_format.pb.h"
#include "hrz/protocol/vector/geometry_type.pb.h"

#include <lin_maths.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <variant>
#include <vector>

namespace vector_tile
{

class Tile;

}

namespace hrz_jobs
{

struct AttributeModel
{
    uint32_t id;
    std::string name;
    hrz_proto::AttributeTransform transform;
    bool is_feature_id;
};

struct ParsedMvt
{
    std::shared_ptr<google::protobuf::Arena> arena;
    vector_tile::Tile* tile;
};

struct VectorDataPackage
{
    // BlobHandle for GeoJSON and Geobuf.
    // ParsedMvt for MVT.
    std::variant<hrz::blobs::BlobHandle, ParsedMvt> data;

    hrz_proto::VectorDataFormat format;
};

struct EncodedVectorTile
{
    // In XYZ format.
    hrz::TileCoords coords;
    VectorDataPackage data;
    std::string layer_name;
    bool decode_geometry;
    std::optional<size_t> source_feature_id_attribute;
    std::vector<AttributeModel> attributes;
};

struct AabbTree
{
    struct Node
    {
        lm::dbbox2 bbox;
        bool has_children;

        union
        {
            uint32_t child_indices[2];
            uint32_t feature_index;
        };
    };

    hrz::BlobArray<Node> aabb_tree;
    hrz::GeoBounds bounds;
};

struct ExtractedVectorTileFeatureInfo
{
    uint32_t source_data_index;
    lm::dbbox2 bbox;
};

struct VectorTileExtractionParams
{
    hrz::TileCoords coords;
    hrz::vector_data::DecodedVectorTile source_data;
    AabbTree aabb_tree;
    float tolerance{};
    bool include_clip_margin{};
    hrz::GeoBounds bounds;
};

struct UnjoinedVectorData
{
    // If true, the features are joined by feature ID,
    // otherwise their order is unchanged, but the number
    // of geometries and attribute values is adjusted if
    // needed to conform to the reference feature IDs count.
    // (If new geometries are needed, empty polygons are
    // used; if new attribute values are needed, nulls are
    // used.)
    bool join_by_feature_ids;

    // This is the reference feature order.
    // The list can contain duplicates, that must be respected.
    hrz::vector_data::FeatureIds reference_feature_ids;

    // The feature order of the geometry and attributes values.
    hrz::vector_data::FeatureIds feature_ids;

    // The features in the geometry will be sorted.
    std::optional<hrz::vector_data::VectorTileGeometry> geometry;

    // These attribute values will be sorted.
    std::vector<hrz::vector_data::AttributeValues> attributes;
};

struct JoinedVectorData
{
    std::optional<hrz::vector_data::VectorTileGeometry> geometry;
    std::vector<hrz::vector_data::AttributeValues> attributes;
};

struct RawClientVectorData
{
    hrz_proto::VectorDataRequestResponse client_data;
    bool expects_geometry;
    std::vector<AttributeModel> attributes;
};

} // namespace hrz_jobs
