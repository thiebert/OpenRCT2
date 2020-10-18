/*****************************************************************************
 * Copyright (c) 2014-2020 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "FootpathItemObject.h"

#include "../core/IStream.hpp"
#include "../core/Json.hpp"
#include "../drawing/Drawing.h"
#include "../interface/Cursors.h"
#include "../localisation/Localisation.h"
#include "../object/Object.h"
#include "../object/ObjectRepository.h"
#include "ObjectList.h"

#include <unordered_map>

void FootpathItemObject::ReadLegacy(IReadObjectContext* context, OpenRCT2::IStream* stream)
{
    stream->Seek(6, OpenRCT2::STREAM_SEEK_CURRENT);
    _legacyType.path_bit.flags = stream->ReadValue<uint16_t>();
    _legacyType.path_bit.draw_type = stream->ReadValue<uint8_t>();
    _legacyType.path_bit.tool_id = static_cast<CursorID>(stream->ReadValue<uint8_t>());
    _legacyType.path_bit.price = stream->ReadValue<int16_t>();
    _legacyType.path_bit.scenery_tab_id = OBJECT_ENTRY_INDEX_NULL;
    stream->Seek(2, OpenRCT2::STREAM_SEEK_CURRENT);

    GetStringTable().Read(context, stream, ObjectStringID::NAME);

    rct_object_entry sgEntry = stream->ReadValue<rct_object_entry>();
    SetPrimarySceneryGroup(&sgEntry);

    GetImageTable().Read(context, stream);

    // Validate properties
    if (_legacyType.large_scenery.price <= 0)
    {
        context->LogError(ObjectError::InvalidProperty, "Price can not be free or negative.");
    }

    // Add path bits to 'Signs and items for footpaths' group, rather than lumping them in the Miscellaneous tab.
    // Since this is already done the other way round for original items, avoid adding those to prevent duplicates.
    auto identifier = GetLegacyIdentifier();

    auto& objectRepository = context->GetObjectRepository();
    auto item = objectRepository.FindObject(identifier);
    if (item != nullptr)
    {
        auto sourceGame = item->GetFirstSourceGame();
        if (sourceGame == ObjectSourceGame::WackyWorlds || sourceGame == ObjectSourceGame::TimeTwister
            || sourceGame == ObjectSourceGame::Custom)
        {
            auto scgPathX = Object::GetScgPathXHeader();
            SetPrimarySceneryGroup(&scgPathX);
        }
    }
}

void FootpathItemObject::Load()
{
    GetStringTable().Sort();
    _legacyType.name = language_allocate_object_string(GetName());
    _legacyType.image = gfx_object_allocate_images(GetImageTable().GetImages(), GetImageTable().GetCount());

    _legacyType.path_bit.scenery_tab_id = OBJECT_ENTRY_INDEX_NULL;
}

void FootpathItemObject::Unload()
{
    language_free_object_string(_legacyType.name);
    gfx_object_free_images(_legacyType.image, GetImageTable().GetCount());

    _legacyType.name = 0;
    _legacyType.image = 0;
}

void FootpathItemObject::DrawPreview(rct_drawpixelinfo* dpi, int32_t width, int32_t height) const
{
    auto screenCoords = ScreenCoordsXY{ width / 2, height / 2 };
    gfx_draw_sprite(dpi, _legacyType.image, screenCoords - ScreenCoordsXY{ 22, 24 }, 0);
}

static uint8_t ParseDrawType(const std::string& s)
{
    if (s == "lamp")
        return PATH_BIT_DRAW_TYPE_LIGHTS;
    if (s == "bin")
        return PATH_BIT_DRAW_TYPE_BINS;
    if (s == "bench")
        return PATH_BIT_DRAW_TYPE_BENCHES;
    if (s == "fountain")
        return PATH_BIT_DRAW_TYPE_JUMPING_FOUNTAINS;
    return PATH_BIT_DRAW_TYPE_LIGHTS;
}

void FootpathItemObject::ReadJson(IReadObjectContext* context, json_t& root)
{
    Guard::Assert(root.is_object(), "FootpathItemObject::ReadJson expects parameter root to be object");

    json_t properties = root["properties"];

    if (properties.is_object())
    {
        _legacyType.path_bit.draw_type = ParseDrawType(Json::GetString(properties["renderAs"]));
        _legacyType.path_bit.tool_id = Cursor::FromString(Json::GetString(properties["cursor"]), CursorID::LamppostDown);
        _legacyType.path_bit.price = Json::GetNumber<int16_t>(properties["price"]);

        SetPrimarySceneryGroup(Json::GetString(properties["sceneryGroup"]));

        // clang-format off
        _legacyType.path_bit.flags = Json::GetFlags<uint16_t>(
            properties,
            {
                { "isBin",                  PATH_BIT_FLAG_IS_BIN,                   Json::FlagType::Normal },
                { "isBench",                PATH_BIT_FLAG_IS_BENCH,                 Json::FlagType::Normal },
                { "isBreakable",            PATH_BIT_FLAG_BREAKABLE,                Json::FlagType::Normal },
                { "isLamp",                 PATH_BIT_FLAG_LAMP,                     Json::FlagType::Normal },
                { "isJumpingFountainWater", PATH_BIT_FLAG_JUMPING_FOUNTAIN_WATER,   Json::FlagType::Normal },
                { "isJumpingFountainSnow",  PATH_BIT_FLAG_JUMPING_FOUNTAIN_SNOW,    Json::FlagType::Normal },
                { "isAllowedOnQueue",       PATH_BIT_FLAG_DONT_ALLOW_ON_QUEUE,      Json::FlagType::Inverted },
                { "isAllowedOnSlope",       PATH_BIT_FLAG_DONT_ALLOW_ON_SLOPE,      Json::FlagType::Inverted },
                { "isTelevision",           PATH_BIT_FLAG_IS_QUEUE_SCREEN,          Json::FlagType::Normal },
            });
        // clang-format on
    }

    PopulateTablesFromJson(context, root);
}
