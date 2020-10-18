/*****************************************************************************
 * Copyright (c) 2014-2020 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "ObjectManager.h"

#include "../Context.h"
#include "../ParkImporter.h"
#include "../core/Console.hpp"
#include "../core/Memory.hpp"
#include "../localisation/StringIds.h"
#include "FootpathItemObject.h"
#include "LargeSceneryObject.h"
#include "Object.h"
#include "ObjectList.h"
#include "ObjectRepository.h"
#include "RideObject.h"
#include "SceneryGroupObject.h"
#include "SmallSceneryObject.h"
#include "WallObject.h"

#include <algorithm>
#include <array>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_set>

class ObjectManager final : public IObjectManager
{
private:
    IObjectRepository& _objectRepository;
    std::vector<std::unique_ptr<Object>> _loadedObjects;
    std::array<std::vector<ObjectEntryIndex>, RIDE_TYPE_COUNT> _rideTypeToObjectMap;

    // Used to return a safe empty vector back from GetAllRideEntries, can be removed when std::span is available
    std::vector<ObjectEntryIndex> _nullRideTypeEntries;

public:
    explicit ObjectManager(IObjectRepository& objectRepository)
        : _objectRepository(objectRepository)
    {
        _loadedObjects.resize(OBJECT_ENTRY_COUNT);

        UpdateSceneryGroupIndexes();
        ResetTypeToRideEntryIndexMap();
    }

    ~ObjectManager() override
    {
        UnloadAll();
    }

    Object* GetLoadedObject(size_t index) override
    {
        if (index >= _loadedObjects.size())
        {
            return nullptr;
        }
        return _loadedObjects[index].get();
    }

    Object* GetLoadedObject(int32_t objectType, size_t index) override
    {
        if (index >= static_cast<size_t>(object_entry_group_counts[objectType]))
        {
#ifdef DEBUG
            log_warning("Object index %u exceeds maximum for type %d.", index, objectType);
#endif
            return nullptr;
        }

        auto objectIndex = GetIndexFromTypeEntry(objectType, index);
        return GetLoadedObject(objectIndex);
    }

    Object* GetLoadedObject(const rct_object_entry* entry) override
    {
        Object* loadedObject = nullptr;
        const ObjectRepositoryItem* ori = _objectRepository.FindObject(entry);
        if (ori != nullptr)
        {
            loadedObject = ori->LoadedObject;
        }
        return loadedObject;
    }

    ObjectEntryIndex GetLoadedObjectEntryIndex(const Object* object) override
    {
        ObjectEntryIndex result = OBJECT_ENTRY_INDEX_NULL;
        size_t index = GetLoadedObjectIndex(object);
        if (index != SIZE_MAX)
        {
            get_type_entry_index(index, nullptr, &result);
        }
        return result;
    }

    Object* LoadObject(const rct_object_entry* entry) override
    {
        Object* loadedObject = nullptr;
        const ObjectRepositoryItem* ori = _objectRepository.FindObject(entry);
        if (ori != nullptr)
        {
            loadedObject = ori->LoadedObject;
            if (loadedObject == nullptr)
            {
                uint8_t objectType = ori->ObjectEntry.GetType();
                int32_t slot = FindSpareSlot(objectType);
                if (slot != -1)
                {
                    auto object = GetOrLoadObject(ori);
                    if (object != nullptr)
                    {
                        if (_loadedObjects.size() <= static_cast<size_t>(slot))
                        {
                            _loadedObjects.resize(slot + 1);
                        }
                        loadedObject = object.get();
                        _loadedObjects[slot] = std::move(object);
                        UpdateSceneryGroupIndexes();
                        ResetTypeToRideEntryIndexMap();
                    }
                }
            }
        }
        return loadedObject;
    }

    void LoadObjects(const rct_object_entry* entries, size_t count) override
    {
        // Find all the required objects
        auto requiredObjects = GetRequiredObjects(entries, count);

        // Load the required objects
        size_t numNewLoadedObjects = 0;
        auto loadedObjects = LoadObjects(requiredObjects, &numNewLoadedObjects);

        SetNewLoadedObjectList(std::move(loadedObjects));
        LoadDefaultObjects();
        UpdateSceneryGroupIndexes();
        ResetTypeToRideEntryIndexMap();
        log_verbose("%u / %u new objects loaded", numNewLoadedObjects, requiredObjects.size());
    }

    void UnloadObjects(const std::vector<rct_object_entry>& entries) override
    {
        // TODO there are two performance issues here:
        //        - FindObject for every entry which is a dictionary lookup
        //        - GetLoadedObjectIndex for every entry which enumerates _loadedList

        size_t numObjectsUnloaded = 0;
        for (const auto& entry : entries)
        {
            const ObjectRepositoryItem* ori = _objectRepository.FindObject(&entry);
            if (ori != nullptr)
            {
                Object* loadedObject = ori->LoadedObject;
                if (loadedObject != nullptr)
                {
                    UnloadObject(loadedObject);
                    numObjectsUnloaded++;
                }
            }
        }

        if (numObjectsUnloaded > 0)
        {
            UpdateSceneryGroupIndexes();
            ResetTypeToRideEntryIndexMap();
        }
    }

    void UnloadAll() override
    {
        for (auto& object : _loadedObjects)
        {
            UnloadObject(object.get());
        }
        UpdateSceneryGroupIndexes();
        ResetTypeToRideEntryIndexMap();
    }

    void ResetObjects() override
    {
        for (auto& loadedObject : _loadedObjects)
        {
            if (loadedObject != nullptr)
            {
                loadedObject->Unload();
                loadedObject->Load();
            }
        }
        UpdateSceneryGroupIndexes();
        ResetTypeToRideEntryIndexMap();
    }

    std::vector<const ObjectRepositoryItem*> GetPackableObjects() override
    {
        std::vector<const ObjectRepositoryItem*> objects;
        size_t numObjects = _objectRepository.GetNumObjects();
        for (size_t i = 0; i < numObjects; i++)
        {
            const ObjectRepositoryItem* item = &_objectRepository.GetObjects()[i];
            if (item->LoadedObject != nullptr && IsObjectCustom(item) && item->LoadedObject->GetLegacyData() != nullptr
                && !item->LoadedObject->IsJsonObject())
            {
                objects.push_back(item);
            }
        }
        return objects;
    }

    void LoadDefaultObjects() override
    {
        // We currently will load new object types here that apply to all
        // loaded RCT1 and RCT2 save files.

        // Surfaces
        LoadObject("#RCT2SGR");
        LoadObject("#RCT2SSY");
        LoadObject("#RCT2SDI");
        LoadObject("#RCT2SRO");
        LoadObject("#RCT2SMA");
        LoadObject("#RCT2SCH");
        LoadObject("#RCT2SGC");
        LoadObject("#RCT2SIC");
        LoadObject("#RCT2SIR");
        LoadObject("#RCT2SIY");
        LoadObject("#RCT2SIP");
        LoadObject("#RCT2SIG");
        LoadObject("#RCT2SSR");
        LoadObject("#RCT2SSA");

        // Edges
        LoadObject("#RCT2ERO");
        LoadObject("#RCT2EWR");
        LoadObject("#RCT2EWB");
        LoadObject("#RCT2EIC");
        LoadObject("#RCT1EBR");
        LoadObject("#RCT1EIR");
        LoadObject("#RCT1EGY");
        LoadObject("#RCT1EYE");
        LoadObject("#RCT1ERE");
        LoadObject("#RCT1EPU");
        LoadObject("#RCT1EGR");
        LoadObject("#RCT1ESN");
        LoadObject("#RCT1ESG");
        LoadObject("#RCT1ESA");
        LoadObject("#RCT1ESB");

        // Stations
        LoadObject("#RCT2STN");
        LoadObject("#RCT2STW");
        LoadObject("#RCT2STV");
        LoadObject("#RCT2ST3");
        LoadObject("#RCT2ST4");
        LoadObject("#RCT2STJ");
        LoadObject("#RCT2STL");
        LoadObject("#RCT2STC");
        LoadObject("#RCT2STA");
        LoadObject("#RCT2STS");
        LoadObject("#RCT2STP");
        LoadObject("#RCT2STE");
        LoadObject("#ORCT2SN");
    }

    static rct_string_id GetObjectSourceGameString(const ObjectSourceGame sourceGame)
    {
        switch (sourceGame)
        {
            case ObjectSourceGame::RCT1:
                return STR_SCENARIO_CATEGORY_RCT1;
            case ObjectSourceGame::AddedAttractions:
                return STR_SCENARIO_CATEGORY_RCT1_AA;
            case ObjectSourceGame::LoopyLandscapes:
                return STR_SCENARIO_CATEGORY_RCT1_LL;
            case ObjectSourceGame::RCT2:
                return STR_ROLLERCOASTER_TYCOON_2_DROPDOWN;
            case ObjectSourceGame::WackyWorlds:
                return STR_OBJECT_FILTER_WW;
            case ObjectSourceGame::TimeTwister:
                return STR_OBJECT_FILTER_TT;
            case ObjectSourceGame::OpenRCT2Official:
                return STR_OBJECT_FILTER_OPENRCT2_OFFICIAL;
            default:
                return STR_OBJECT_FILTER_CUSTOM;
        }
    }

    const std::vector<ObjectEntryIndex>& GetAllRideEntries(uint8_t rideType) override
    {
        if (rideType >= RIDE_TYPE_COUNT)
        {
            // Return an empty vector
            return _nullRideTypeEntries;
        }
        return _rideTypeToObjectMap[rideType];
    }

private:
    Object* LoadObject(const std::string& name)
    {
        rct_object_entry entry{};
        std::copy_n(name.c_str(), 8, entry.name);
        return LoadObject(&entry);
    }

    int32_t FindSpareSlot(uint8_t objectType)
    {
        size_t firstIndex = GetIndexFromTypeEntry(objectType, 0);
        size_t endIndex = firstIndex + object_entry_group_counts[objectType];
        for (size_t i = firstIndex; i < endIndex; i++)
        {
            if (_loadedObjects.size() <= i)
            {
                _loadedObjects.resize(i + 1);
                return static_cast<int32_t>(i);
            }
            else if (_loadedObjects[i] == nullptr)
            {
                return static_cast<int32_t>(i);
            }
        }
        return -1;
    }

    size_t GetLoadedObjectIndex(const Object* object)
    {
        Guard::ArgumentNotNull(object, GUARD_LINE);

        auto result = std::numeric_limits<size_t>().max();
        auto it = std::find_if(
            _loadedObjects.begin(), _loadedObjects.end(), [object](auto& obj) { return obj.get() == object; });
        if (it != _loadedObjects.end())
        {
            result = std::distance(_loadedObjects.begin(), it);
        }
        return result;
    }

    void SetNewLoadedObjectList(std::vector<std::unique_ptr<Object>>&& newLoadedObjects)
    {
        if (newLoadedObjects.empty())
        {
            UnloadAll();
        }
        else
        {
            UnloadObjectsExcept(newLoadedObjects);
        }
        _loadedObjects = std::move(newLoadedObjects);
    }

    void UnloadObject(Object* object)
    {
        if (object != nullptr)
        {
            object->Unload();

            // TODO try to prevent doing a repository search
            const ObjectRepositoryItem* ori = _objectRepository.FindObject(object->GetObjectEntry());
            if (ori != nullptr)
            {
                _objectRepository.UnregisterLoadedObject(ori, object);
            }

            // Because it's possible to have the same loaded object for multiple
            // slots, we have to make sure find and set all of them to nullptr
            for (auto& obj : _loadedObjects)
            {
                if (obj.get() == object)
                {
                    obj = nullptr;
                }
            }
        }
    }

    void UnloadObjectsExcept(const std::vector<std::unique_ptr<Object>>& newLoadedObjects)
    {
        // Build a hash set for quick checking
        auto exceptSet = std::unordered_set<Object*>();
        for (auto& object : newLoadedObjects)
        {
            if (object != nullptr)
            {
                exceptSet.insert(object.get());
            }
        }

        // Unload objects that are not in the hash set
        size_t totalObjectsLoaded = 0;
        size_t numObjectsUnloaded = 0;
        for (auto& object : _loadedObjects)
        {
            if (object != nullptr)
            {
                totalObjectsLoaded++;
                if (exceptSet.find(object.get()) == exceptSet.end())
                {
                    UnloadObject(object.get());
                    numObjectsUnloaded++;
                }
            }
        }

        log_verbose("%u / %u objects unloaded", numObjectsUnloaded, totalObjectsLoaded);
    }

    void UpdateSceneryGroupIndexes()
    {
        for (auto& loadedObject : _loadedObjects)
        {
            if (loadedObject != nullptr)
            {
                rct_scenery_entry* sceneryEntry;
                switch (loadedObject->GetObjectType())
                {
                    case OBJECT_TYPE_SMALL_SCENERY:
                        sceneryEntry = static_cast<rct_scenery_entry*>(loadedObject->GetLegacyData());
                        sceneryEntry->small_scenery.scenery_tab_id = GetPrimarySceneryGroupEntryIndex(loadedObject.get());
                        break;
                    case OBJECT_TYPE_LARGE_SCENERY:
                        sceneryEntry = static_cast<rct_scenery_entry*>(loadedObject->GetLegacyData());
                        sceneryEntry->large_scenery.scenery_tab_id = GetPrimarySceneryGroupEntryIndex(loadedObject.get());
                        break;
                    case OBJECT_TYPE_WALLS:
                        sceneryEntry = static_cast<rct_scenery_entry*>(loadedObject->GetLegacyData());
                        sceneryEntry->wall.scenery_tab_id = GetPrimarySceneryGroupEntryIndex(loadedObject.get());
                        break;
                    case OBJECT_TYPE_BANNERS:
                        sceneryEntry = static_cast<rct_scenery_entry*>(loadedObject->GetLegacyData());
                        sceneryEntry->banner.scenery_tab_id = GetPrimarySceneryGroupEntryIndex(loadedObject.get());
                        break;
                    case OBJECT_TYPE_PATH_BITS:
                        sceneryEntry = static_cast<rct_scenery_entry*>(loadedObject->GetLegacyData());
                        sceneryEntry->path_bit.scenery_tab_id = GetPrimarySceneryGroupEntryIndex(loadedObject.get());
                        break;
                    case OBJECT_TYPE_SCENERY_GROUP:
                        auto sgObject = dynamic_cast<SceneryGroupObject*>(loadedObject.get());
                        sgObject->UpdateEntryIndexes();
                        break;
                }
            }
        }

        // HACK Scenery window will lose its tabs after changing the scenery group indexing
        //      for now just close it, but it will be better to later tell it to invalidate the tabs
        window_close_by_class(WC_SCENERY);
    }

    ObjectEntryIndex GetPrimarySceneryGroupEntryIndex(Object* loadedObject)
    {
        auto sceneryObject = dynamic_cast<SceneryObject*>(loadedObject);
        const rct_object_entry* primarySGEntry = sceneryObject->GetPrimarySceneryGroup();
        Object* sgObject = GetLoadedObject(primarySGEntry);

        auto entryIndex = OBJECT_ENTRY_INDEX_NULL;
        if (sgObject != nullptr)
        {
            entryIndex = GetLoadedObjectEntryIndex(sgObject);
        }
        return entryIndex;
    }

    rct_object_entry* DuplicateObjectEntry(const rct_object_entry* original)
    {
        rct_object_entry* duplicate = Memory::Allocate<rct_object_entry>(sizeof(rct_object_entry));
        duplicate->checksum = original->checksum;
        strncpy(duplicate->name, original->name, 8);
        duplicate->flags = original->flags;
        return duplicate;
    }

    std::vector<rct_object_entry> GetInvalidObjects(const rct_object_entry* entries) override
    {
        std::vector<rct_object_entry> invalidEntries;
        invalidEntries.reserve(OBJECT_ENTRY_COUNT);
        for (int32_t i = 0; i < OBJECT_ENTRY_COUNT; i++)
        {
            auto entry = entries[i];
            const ObjectRepositoryItem* ori = nullptr;
            if (object_entry_is_empty(&entry))
            {
                entry = {};
                continue;
            }

            ori = _objectRepository.FindObject(&entry);
            if (ori == nullptr)
            {
                if (entry.GetType() != OBJECT_TYPE_SCENARIO_TEXT)
                {
                    invalidEntries.push_back(entry);
                    ReportMissingObject(&entry);
                }
                else
                {
                    entry = {};
                    continue;
                }
            }
            else
            {
                auto loadedObject = ori->LoadedObject;
                if (loadedObject == nullptr)
                {
                    auto object = _objectRepository.LoadObject(ori);
                    if (object == nullptr)
                    {
                        invalidEntries.push_back(entry);
                        ReportObjectLoadProblem(&entry);
                    }
                }
            }
        }
        return invalidEntries;
    }

    std::vector<const ObjectRepositoryItem*> GetRequiredObjects(const rct_object_entry* entries, size_t count)
    {
        std::vector<const ObjectRepositoryItem*> requiredObjects;
        std::vector<rct_object_entry> missingObjects;

        for (size_t i = 0; i < count; i++)
        {
            const rct_object_entry* entry = &entries[i];
            const ObjectRepositoryItem* ori = nullptr;
            if (!object_entry_is_empty(entry))
            {
                ori = _objectRepository.FindObject(entry);
                if (ori == nullptr && entry->GetType() != OBJECT_TYPE_SCENARIO_TEXT)
                {
                    missingObjects.push_back(*entry);
                    ReportMissingObject(entry);
                }
            }
            requiredObjects.push_back(ori);
        }

        if (!missingObjects.empty())
        {
            throw ObjectLoadException(std::move(missingObjects));
        }

        return requiredObjects;
    }

    template<typename T, typename TFunc> static void ParallelFor(const std::vector<T>& items, TFunc func)
    {
        auto partitions = std::thread::hardware_concurrency();
        auto partitionSize = (items.size() + (partitions - 1)) / partitions;
        std::vector<std::thread> threads;
        for (size_t n = 0; n < partitions; n++)
        {
            auto begin = n * partitionSize;
            auto end = std::min(items.size(), begin + partitionSize);
            threads.emplace_back(
                [func](size_t pbegin, size_t pend) {
                    for (size_t i = pbegin; i < pend; i++)
                    {
                        func(i);
                    }
                },
                begin, end);
        }
        for (auto& t : threads)
        {
            t.join();
        }
    }

    std::vector<std::unique_ptr<Object>> LoadObjects(
        std::vector<const ObjectRepositoryItem*>& requiredObjects, size_t* outNewObjectsLoaded)
    {
        std::vector<std::unique_ptr<Object>> objects;
        std::vector<Object*> loadedObjects;
        std::vector<rct_object_entry> badObjects;
        objects.resize(OBJECT_ENTRY_COUNT);
        loadedObjects.reserve(OBJECT_ENTRY_COUNT);

        // Read objects
        std::mutex commonMutex;
        ParallelFor(requiredObjects, [this, &commonMutex, requiredObjects, &objects, &badObjects, &loadedObjects](size_t i) {
            auto requiredObject = requiredObjects[i];
            std::unique_ptr<Object> object;
            if (requiredObject != nullptr)
            {
                auto loadedObject = requiredObject->LoadedObject;
                if (loadedObject == nullptr)
                {
                    // Object requires to be loaded, if the object successfully loads it will register it
                    // as a loaded object otherwise placed into the badObjects list.
                    object = _objectRepository.LoadObject(requiredObject);
                    std::lock_guard<std::mutex> guard(commonMutex);
                    if (object == nullptr)
                    {
                        badObjects.push_back(requiredObject->ObjectEntry);
                        ReportObjectLoadProblem(&requiredObject->ObjectEntry);
                    }
                    else
                    {
                        loadedObjects.push_back(object.get());
                        // Connect the ori to the registered object
                        _objectRepository.RegisterLoadedObject(requiredObject, object.get());
                    }
                }
                else
                {
                    // The object is already loaded, given that the new list will be used as the next loaded object list,
                    // we can move the element out safely. This is required as the resulting list must contain all loaded
                    // objects and not just the newly loaded ones.
                    std::lock_guard<std::mutex> guard(commonMutex);
                    auto it = std::find_if(_loadedObjects.begin(), _loadedObjects.end(), [loadedObject](const auto& obj) {
                        return obj.get() == loadedObject;
                    });
                    if (it != _loadedObjects.end())
                    {
                        object = std::move(*it);
                    }
                }
            }
            objects[i] = std::move(object);
        });

        // Load objects
        for (auto obj : loadedObjects)
        {
            obj->Load();
        }

        if (!badObjects.empty())
        {
            // Unload all the new objects we loaded
            for (auto object : loadedObjects)
            {
                UnloadObject(object);
            }
            throw ObjectLoadException(std::move(badObjects));
        }

        if (outNewObjectsLoaded != nullptr)
        {
            *outNewObjectsLoaded = loadedObjects.size();
        }
        return objects;
    }

    std::unique_ptr<Object> GetOrLoadObject(const ObjectRepositoryItem* ori)
    {
        std::unique_ptr<Object> object;
        auto loadedObject = ori->LoadedObject;
        if (loadedObject == nullptr)
        {
            // Try to load object
            object = _objectRepository.LoadObject(ori);
            if (object != nullptr)
            {
                object->Load();

                // Connect the ori to the registered object
                _objectRepository.RegisterLoadedObject(ori, object.get());
            }
        }
        return object;
    }

    void ResetTypeToRideEntryIndexMap()
    {
        // Clear all ride objects
        for (auto& v : _rideTypeToObjectMap)
        {
            v.clear();
        }

        // Build object lists
        auto maxRideObjects = static_cast<size_t>(object_entry_group_counts[OBJECT_TYPE_RIDE]);
        for (size_t i = 0; i < maxRideObjects; i++)
        {
            auto rideObject = static_cast<RideObject*>(GetLoadedObject(OBJECT_TYPE_RIDE, i));
            if (rideObject != nullptr)
            {
                const auto entry = static_cast<rct_ride_entry*>(rideObject->GetLegacyData());
                if (entry != nullptr)
                {
                    for (auto rideType : entry->ride_type)
                    {
                        if (rideType < _rideTypeToObjectMap.size())
                        {
                            auto& v = _rideTypeToObjectMap[rideType];
                            v.push_back(static_cast<ObjectEntryIndex>(i));
                        }
                    }
                }
            }
        }
    }

    static void ReportMissingObject(const rct_object_entry* entry)
    {
        utf8 objName[DAT_NAME_LENGTH + 1] = { 0 };
        std::copy_n(entry->name, DAT_NAME_LENGTH, objName);
        Console::Error::WriteLine("[%s] Object not found.", objName);
    }

    void ReportObjectLoadProblem(const rct_object_entry* entry)
    {
        utf8 objName[DAT_NAME_LENGTH + 1] = { 0 };
        std::copy_n(entry->name, DAT_NAME_LENGTH, objName);
        Console::Error::WriteLine("[%s] Object could not be loaded.", objName);
    }

    static int32_t GetIndexFromTypeEntry(int32_t objectType, size_t entryIndex)
    {
        int32_t result = 0;
        for (int32_t i = 0; i < objectType; i++)
        {
            result += object_entry_group_counts[i];
        }
        result += static_cast<int32_t>(entryIndex);
        return result;
    }
};

std::unique_ptr<IObjectManager> CreateObjectManager(IObjectRepository& objectRepository)
{
    return std::make_unique<ObjectManager>(objectRepository);
}

Object* object_manager_get_loaded_object_by_index(size_t index)
{
    auto& objectManager = OpenRCT2::GetContext()->GetObjectManager();
    Object* loadedObject = objectManager.GetLoadedObject(index);
    return loadedObject;
}

Object* object_manager_get_loaded_object(const rct_object_entry* entry)
{
    auto& objectManager = OpenRCT2::GetContext()->GetObjectManager();
    Object* loadedObject = objectManager.GetLoadedObject(entry);
    return loadedObject;
}

ObjectEntryIndex object_manager_get_loaded_object_entry_index(const void* loadedObject)
{
    auto& objectManager = OpenRCT2::GetContext()->GetObjectManager();
    const Object* object = static_cast<const Object*>(loadedObject);
    auto entryIndex = objectManager.GetLoadedObjectEntryIndex(object);
    return entryIndex;
}

void* object_manager_load_object(const rct_object_entry* entry)
{
    auto& objectManager = OpenRCT2::GetContext()->GetObjectManager();
    Object* loadedObject = objectManager.LoadObject(entry);
    return static_cast<void*>(loadedObject);
}

void object_manager_unload_objects(const std::vector<rct_object_entry>& entries)
{
    auto& objectManager = OpenRCT2::GetContext()->GetObjectManager();
    objectManager.UnloadObjects(entries);
}

void object_manager_unload_all_objects()
{
    auto& objectManager = OpenRCT2::GetContext()->GetObjectManager();
    objectManager.UnloadAll();
}

rct_string_id object_manager_get_source_game_string(const ObjectSourceGame sourceGame)
{
    return ObjectManager::GetObjectSourceGameString(sourceGame);
}
