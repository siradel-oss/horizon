#include "hrz_jobs_declarations.h"
#include "vector_tile.pb.h"

#include <hrz_common_blob_allocator.h>
#include <hrz_common_profiling.h>
#include <hrz_common_vector_data.h>

namespace hrz_jobs::parse_mvt
{
hrz::JobResult run(
    const hrz::blobs::BlobHandle& raw_data_blob,
    hrz::vector_data::ParsedMvt& parsed_mvt,
    const JobContext& context)
{
    HRZ_SCOPED_SAMPLE("parse mvt job");

    parsed_mvt.arena = std::make_shared<google::protobuf::Arena>();
    parsed_mvt.tile = google::protobuf::Arena::Create<vector_tile::Tile>(parsed_mvt.arena.get());

    auto raw_data = raw_data_blob.get_data();

    if (parsed_mvt.tile->ParseFromArray(raw_data.data(), raw_data.size()))
    {
        return hrz::JobResult::SUCCESS;
    }
    else
    {
        return hrz::JobResult::FAILURE;
    }
}
} // namespace hrz_jobs::parse_mvt
