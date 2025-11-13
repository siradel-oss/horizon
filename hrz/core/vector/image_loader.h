#pragma once

#include "hrz/common/monitoring_defs.h"
#include "hrz/protocol/all.h"

#include <lin_maths.h>
#include <mycelium/mycelium.h>

#include <cstdint>
#include <string_view>

namespace hrz
{
struct AssetsLoader;
struct BlobAllocator;
struct JobScheduler;
struct Render;

namespace vt
{
struct ImageLoader;

namespace image_loader
{
using ImageH = uint32_t;

enum class ImageStatus
{
    Loading,
    Loaded,
    Error,
};

struct Texture
{
    my::ResourceHandle texture;
    lm::uvec2 size;
};

ImageLoader* create_loader();
void destroy_loader(ImageLoader*);

ImageH load_image(
    ImageLoader*,
    std::string_view url,
    const hrz_proto::HttpHeaderList&,
    const monitoring::ResourceOwner&);
void release_image(ImageLoader*, ImageH);

bool is_image_valid(ImageLoader*, ImageH);

ImageStatus get_image_status(ImageLoader*, ImageH);
Texture get_image_texture(ImageLoader*, ImageH);

void work(ImageLoader*, AssetsLoader*, BlobAllocator*, JobScheduler*);
void work_gpu(ImageLoader*, Render*);
} // namespace image_loader
} // namespace vt
} // namespace hrz
