/*****************************************************************************
 * Copyright (c) 2014-2020 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "../common.h"
#include "../object/ObjectLimits.h"
#include "../ride/Ride.h"

#include <optional>

struct rct_ride_entry;

namespace Research
{
    enum class EntryType : uint8_t
    {
        Scenery = 0,
        Ride = 1,
    };
}

enum
{
    RESEARCH_ENTRY_FLAG_FIRST_OF_TYPE = (1 << 0),
    RESEARCH_ENTRY_FLAG_SCENERY_SET_ALWAYS_RESEARCHED = (1 << 5),
    RESEARCH_ENTRY_FLAG_RIDE_ALWAYS_RESEARCHED = (1 << 6),
};

struct ResearchItem
{
    union
    {
        uint32_t rawValue;
        struct
        {
            ObjectEntryIndex entryIndex;
            uint8_t baseRideType;
            Research::EntryType type; // 0: scenery entry, 1: ride entry
        };
    };
    uint8_t flags;
    uint8_t category;

    bool IsNull() const;
    void SetNull();
    bool Equals(const ResearchItem* otherItem) const;
    bool Exists() const;
    bool IsAlwaysResearched() const;
    rct_string_id GetName() const;

    ResearchItem() = default;
    constexpr ResearchItem(uint32_t _rawValue, uint8_t _category, uint8_t _flags)
        : rawValue(_rawValue)
        , flags(_flags)
        , category(_category)
    {
    }
    ResearchItem(
        Research::EntryType _type, ObjectEntryIndex _entryIndex, uint8_t _baseRideType, uint8_t _category, uint8_t _flags)
        : entryIndex(_entryIndex)
        , baseRideType(_baseRideType)
        , type(_type)
        , flags(_flags)
        , category(_category)
    {
    }

    RCT12ResearchItem ToRCT12ResearchItem() const
    {
        RCT12ResearchItem retItem = {};
        if (IsNull())
        {
            retItem.rawValue = RCT12_RESEARCHED_ITEMS_SEPARATOR;
        }
        else
        {
            retItem.entryIndex = OpenRCT2EntryIndexToRCTEntryIndex(entryIndex);
            retItem.baseRideType = OpenRCT2RideTypeToRCT2RideType(baseRideType);
            retItem.type = static_cast<uint8_t>(type);
            retItem.flags = (flags & ~RESEARCH_ENTRY_FLAG_FIRST_OF_TYPE);
            retItem.category = category;
        }

        return retItem;
    }

    ResearchItem(const RCT12ResearchItem& oldResearchItem)
    {
        if (oldResearchItem.IsInventedEndMarker() || oldResearchItem.IsUninventedEndMarker()
            || oldResearchItem.IsRandomEndMarker())
        {
            rawValue = 0;
            flags = 0;
            category = 0;
            SetNull();
        }
        else
        {
            entryIndex = RCTEntryIndexToOpenRCT2EntryIndex(oldResearchItem.entryIndex);
            auto* rideEntry = get_ride_entry(entryIndex);
            baseRideType = rideEntry != nullptr ? RCT2RideTypeToOpenRCT2RideType(oldResearchItem.baseRideType, rideEntry)
                                                : oldResearchItem.baseRideType;
            type = Research::EntryType{ oldResearchItem.type };
            flags = oldResearchItem.flags;
            category = oldResearchItem.category;
        }
    }
};

// Only used to mark as null nowadays. Deprecated. TODO: remove.
#define RESEARCH_ITEM_NULL 0xFFFFFFFF

#define MAX_RESEARCH_ITEMS 500

enum
{
    RESEARCH_FUNDING_NONE,
    RESEARCH_FUNDING_MINIMUM,
    RESEARCH_FUNDING_NORMAL,
    RESEARCH_FUNDING_MAXIMUM,

    RESEARCH_FUNDING_COUNT
};

enum
{
    RESEARCH_STAGE_INITIAL_RESEARCH,
    RESEARCH_STAGE_DESIGNING,
    RESEARCH_STAGE_COMPLETING_DESIGN,
    RESEARCH_STAGE_UNKNOWN,
    RESEARCH_STAGE_FINISHED_ALL
};

enum class ResearchCategory : uint8_t
{
    Transport,
    Gentle,
    Rollercoaster,
    Thrill,
    Water,
    Shop,
    Group
};

extern uint8_t gResearchFundingLevel;
extern uint8_t gResearchPriorities;
extern uint16_t gResearchProgress;
extern uint8_t gResearchProgressStage;
extern uint8_t gResearchExpectedMonth;
extern uint8_t gResearchExpectedDay;
extern std::optional<ResearchItem> gResearchLastItem;
extern std::optional<ResearchItem> gResearchNextItem;

extern std::vector<ResearchItem> gResearchItemsUninvented;
extern std::vector<ResearchItem> gResearchItemsInvented;
extern uint8_t gResearchUncompletedCategories;
extern bool gSilentResearch;

void research_reset_items();
void research_update_uncompleted_types();
void research_update();
void research_reset_current_item();
void research_populate_list_random();

void research_finish_item(ResearchItem* researchItem);
void research_insert(ResearchItem item, bool researched);
void research_remove(ResearchItem* researchItem);

bool research_insert_ride_entry(uint8_t rideType, ObjectEntryIndex entryIndex, uint8_t category, bool researched);
void research_insert_ride_entry(ObjectEntryIndex entryIndex, bool researched);
bool research_insert_scenery_group_entry(ObjectEntryIndex entryIndex, bool researched);

void ride_type_set_invented(uint32_t rideType);
void ride_entry_set_invented(int32_t rideEntryIndex);
void scenery_set_invented(const ScenerySelection& sceneryItem);
void scenery_set_not_invented(const ScenerySelection& sceneryItem);
bool ride_type_is_invented(uint32_t rideType);
bool ride_entry_is_invented(int32_t rideEntryIndex);
bool scenery_group_is_invented(int32_t sgIndex);
void scenery_group_set_invented(int32_t sgIndex);
bool scenery_is_invented(const ScenerySelection& sceneryItem);
void set_all_scenery_items_invented();
void set_all_scenery_items_not_invented();
void set_all_scenery_groups_not_invented();
void set_every_ride_type_invented();
void set_every_ride_type_not_invented();
void set_every_ride_entry_invented();
void set_every_ride_entry_not_invented();
void research_remove_flags();
void research_fix();

void research_items_make_all_unresearched();
void research_items_make_all_researched();
void research_items_shuffle();
/**
 * Determines if a newly invented ride entry should be listed as a new ride
 * or as a new vehicle for a pre-existing ride.
 */
void research_determine_first_of_type();
