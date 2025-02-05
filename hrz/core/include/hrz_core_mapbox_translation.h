#pragma once

#include <hrz_protocol_all.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace hrz
{
struct MapboxTranslationSystem;
struct ActorRunner;
struct AssetsLoader;
struct BlobAllocator;
struct ClientMessageQueue;
struct Scene;
struct SceneModel;

namespace mapbox
{
using TranslationTicket = uint64_t;

MapboxTranslationSystem* create_translation_system();

void destroy_translation_system(MapboxTranslationSystem*);

TranslationTicket begin_translation(
    MapboxTranslationSystem*,
    AssetsLoader*,
    ClientMessageQueue*,
    const hrz_proto::MapboxTranslationParams&);

void work(
    MapboxTranslationSystem*,
    AssetsLoader*,
    BlobAllocator*,
    ClientMessageQueue*,
    Scene*,
    ActorRunner*);

bool is_working(const MapboxTranslationSystem*);

} // namespace mapbox
} // namespace hrz
