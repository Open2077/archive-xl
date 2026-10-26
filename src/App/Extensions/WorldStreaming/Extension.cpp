#include "Extension.hpp"
#include "App/Extensions/ResourcePatch/Extension.hpp"
#include "Red/CommunitySystem.hpp"

namespace
{
constexpr auto ExtensionName = "WorldStreaming";

constexpr auto MainWorldResource = Red::ResourcePath(R"(base\worlds\03_night_city\_compiled\default\03_night_city.streamingworld)");
constexpr auto CollisionNodeType = Red::GetTypeName<Red::worldCollisionNode>();
constexpr auto AdvertisementNodeType = Red::GetTypeName<Red::worldAdvertisementNode>();
constexpr auto StaticMeshNodeType = Red::GetTypeName<Red::worldStaticMeshNode>();
constexpr auto InstancedMeshNodeType = Red::GetTypeName<Red::worldInstancedMeshNode>();
constexpr auto InstancedDestructibleNodeType = Red::GetTypeName<Red::worldInstancedDestructibleMeshNode>();
constexpr auto CookedCollisionNodeType = Red::CName("worldCookedCollisionNode");
constexpr auto CookedStaticMeshNodeType = Red::CName("worldCookedStaticMeshNode");

constexpr auto DelZ = static_cast<int32_t>(-2000 * (2 << 16));
constexpr auto NoCollisionPreset = Red::CName("No Collision");

Red::Handle<Red::worldStaticMeshNode> s_dummyNode;
Core::Map<Red::ResourcePath, Core::Map<int64_t, Red::CName*>> s_collisionNodeOverrides;
}

std::string_view App::WorldStreamingExtension::GetName()
{
    return ExtensionName;
}

bool App::WorldStreamingExtension::Load()
{
    HookAfter<Raw::StreamingSector::PostLoad>(&OnSectorPostLoad).OrThrow();
    Hook<Raw::AIWorkspotManager::RegisterSpots>(&OnRegisterSpots).OrThrow();

    s_dummyNode = Red::MakeHandle<Red::worldStaticMeshNode>();
    s_dummyNode->isVisibleInGame = false;

    return true;
}

bool App::WorldStreamingExtension::Unload()
{
    Unhook<Raw::StreamingSector::PostLoad>();
    Unhook<Raw::AIWorkspotManager::RegisterSpots>();

    return true;
}

void App::WorldStreamingExtension::Configure()
{
    std::unique_lock _(s_sectorsLock);

    s_sectors.clear();

    auto depot = Red::ResourceDepot::Get();
    auto rtti = Red::CRTTISystem::Get();

    Core::Set<Red::ResourcePath> invalidPaths;
    Core::Set<Red::CName> invalidNodeTypes;

    for (auto& unit : m_configs)
    {
        for (auto& sectorMod : unit.sectors)
        {
            auto sectorPath = Red::ResourcePath(sectorMod.path.data());

            if (!depot->ResourceExists(sectorPath))
            {
                if (!invalidPaths.contains(sectorPath))
                {
                    LogError("[{}] {}: Sector \"{}\" doesn't exist.",
                             ExtensionName, sectorMod.mod, sectorMod.path);
                    invalidPaths.insert(sectorPath);
                }
                continue;
            }

            bool allNodeTypesValid = true;

            for (const auto& nodeDeletion : sectorMod.nodeDeletions)
            {
                if (!rtti->GetClass(nodeDeletion.nodeType))
                {
                    allNodeTypesValid = false;

                    if (!invalidNodeTypes.contains(nodeDeletion.nodeType))
                    {
                        LogError("[{}] {}: Node type \"{}\" doesn't exist.",
                                 ExtensionName, sectorMod.mod, nodeDeletion.nodeType.ToString());
                        invalidNodeTypes.insert(nodeDeletion.nodeType);
                    }
                }
            }

            if (!allNodeTypesValid)
                continue;

            s_sectors[sectorPath].emplace_back(std::move(sectorMod));
        }

        unit.sectors.clear();
    }
}

void App::WorldStreamingExtension::PostConfigure()
{
    // ResourcePatchExtension::ClearTarget(MainWorldResource);

    for (auto& unit : m_configs)
    {
        for (auto& blockPathStr : unit.blocks)
        {
            ResourcePatchExtension::RegisterPatch(MainWorldResource, blockPathStr.data());
        }

        unit.blocks.clear();
    }
}

void App::WorldStreamingExtension::OnSectorPostLoad(Red::world::StreamingSector* aSector, uint64_t)
{
    std::shared_lock _(s_sectorsLock);

    const auto& sectorMods = s_sectors.find(aSector->path);

    if (sectorMods == s_sectors.end())
        return;

    const auto& sectorPath = sectorMods.value().begin()->path;
    auto patchedAny = false;
    auto successAll = true;

    LogInfo("[{}] Patching sector \"{}\"...", ExtensionName, sectorPath);

    for (const auto& sectorMod : sectorMods.value())
    {
        LogInfo("[{}] Applying changes from \"{}\"...", ExtensionName, sectorMod.mod);

        if (PatchSector(aSector, sectorMod))
        {
            patchedAny = true;
        }
        else
        {
            successAll = false;
        }
    }

    if (successAll)
        LogInfo("[{}] All patches have been applied to \"{}\".", ExtensionName, sectorPath);
    else if (patchedAny)
        LogWarning("[{}] Some patches have not been applied to \"{}\".", ExtensionName, sectorPath);
    else
        LogWarning("[{}] No patches have been applied to \"{}\".", ExtensionName, sectorPath);
}

namespace
{
// Patch 2.5 cooks the world: a cooked node (worldCooked*, worldInstancedMeshNode) holds the entries
// of several placements, each placement picking its entry by its instance index.
bool IsCooked(Red::worldNode* aNode)
{
    return std::string_view(aNode->GetNativeType()->name.ToString()).starts_with("worldCooked");
}

struct CollisionRun
{
    Red::CollisionActor* actors;
    uint32_t numActors;
    Red::CollisionShape* shapes;
    uint32_t numShapes;
    Red::CName* presets;
    uint32_t numPresets;
};

// The actors of one placement of a cooked collision node, in the node's compiled data.
bool GetCollisionRun(Red::worldNode* aNode, uint32_t aInstanceIndex, CollisionRun& aRun)
{
    auto& entries = Raw::CookedCollisionNode::Entries::Ref(aNode);
    auto stride = Raw::CookedCollisionNode::EntryStride::Ref(aNode);
    if (stride != sizeof(Red::CookedCollisionEntry) ||
        (static_cast<uint64_t>(aInstanceIndex) + 1) * stride > entries.Size())
        return false;

    const auto* entry = reinterpret_cast<const Red::CookedCollisionEntry*>(entries.Data() + aInstanceIndex * stride);
    auto& data = Raw::CookedCollisionNode::CompiledData::Ref(aNode);
    auto* base = static_cast<uint8_t*>(data.buffer.data);

    const uint64_t shapesOffset = entry->dataOffset + entry->numActors * 0x30ull;
    const uint64_t presetsOffset = shapesOffset + entry->numShapeInfos * 0x20ull + entry->numShapePositions * 12ull +
                                   entry->numShapeRotations * 16ull + entry->numScales * 12ull + entry->numMaterials * 8ull;
    if (!base || presetsOffset + entry->numPresets * 8ull > data.buffer.size)
        return false;

    aRun.actors = reinterpret_cast<Red::CollisionActor*>(base + entry->dataOffset);
    aRun.numActors = entry->numActors;
    aRun.shapes = reinterpret_cast<Red::CollisionShape*>(base + shapesOffset);
    aRun.numShapes = entry->numShapeInfos;
    aRun.presets = reinterpret_cast<Red::CName*>(base + presetsOffset);
    aRun.numPresets = entry->numPresets;
    return true;
}

// The instances of one placement of a cooked instanced mesh node, in the node's world transforms.
Red::RenderProxyTransform* GetInstanceRun(Red::worldNode* aNode, uint32_t aInstanceIndex, uint32_t& aCount)
{
    auto& entries = Raw::InstancedMeshNode::Entries::Ref(aNode);
    auto& ranges = Raw::InstancedMeshNode::Ranges::Ref(aNode);
    auto& transforms = Raw::InstancedMeshNode::Transforms::Ref(aNode);
    if (aInstanceIndex >= entries.Size() || aInstanceIndex >= ranges.Size())
        return nullptr;

    const auto& range = ranges.Data()[aInstanceIndex];
    if (static_cast<uint32_t>(range.startIndex) + range.numElements > transforms.GetCount())
        return nullptr;

    aCount = range.numElements;
    return transforms.Get(range.startIndex);
}

uint32_t CountElements(Red::worldNode* aNode, Red::CompiledNodeInstanceSetupInfo* aSetup, Red::CName aType, bool& aSupported)
{
    aSupported = true;
    if (aType == CookedCollisionNodeType)
    {
        CollisionRun run{};
        aSupported = GetCollisionRun(aNode, aSetup->instanceIndex, run);
        return run.numActors;
    }
    if (aType == InstancedMeshNodeType)
    {
        uint32_t count = 0;
        aSupported = GetInstanceRun(aNode, aSetup->instanceIndex, count) != nullptr;
        return count;
    }
    if (aType == InstancedDestructibleNodeType)
    {
        return Raw::InstancedDestructibleMeshNode::InstanceTransforms::Ref(aNode).Size();
    }
    aSupported = false;
    return 0;
}
}

bool App::WorldStreamingExtension::PatchSector(Red::world::StreamingSector* aSector,
                                               const App::WorldSectorMod& aSectorMod)
{
    auto& buffer = Raw::StreamingSector::NodeBuffer::Ref(aSector);
    auto nodeCount = buffer.nodeSetups.GetInstanceCount();

    if (nodeCount != aSectorMod.expectedNodes)
    {
        LogError(R"([{}] {}: The target sector has {} node(s), but the mod expects {}.)",
                 ExtensionName, aSectorMod.mod, nodeCount, aSectorMod.expectedNodes);
        return false;
    }

    auto resolve = [&](int64_t aIndex, Red::CName aExpectedType,
                       Red::CompiledNodeInstanceSetupInfo*& aSetup, Red::worldNode*& aDefinition) -> bool {
        aSetup = buffer.nodeSetups.GetInstance(aIndex);
        auto definitionIndex = aSetup->GetNodeIndex();
        if (definitionIndex >= buffer.nodes.Size() || !buffer.nodes[definitionIndex].instance)
        {
            LogError(R"([{}] {}: The target node #{} has no definition.)", ExtensionName, aSectorMod.mod, aIndex);
            return false;
        }
        aDefinition = buffer.nodes[definitionIndex].instance;
        if (aExpectedType != aDefinition->GetNativeType()->name)
        {
            LogError(R"([{}] {}: The target node #{} has type {}, but the mod expects {}.)",
                     ExtensionName, aSectorMod.mod, aIndex,
                     aDefinition->GetNativeType()->name.ToString(), aExpectedType.ToString());
            return false;
        }
        return true;
    };

    auto checkElements = [&](int64_t aIndex, Red::CName aType, Red::CompiledNodeInstanceSetupInfo* aSetup,
                             Red::worldNode* aDefinition, int64_t aExpected) -> bool {
        bool supported = false;
        auto count = CountElements(aDefinition, aSetup, aType, supported);
        if (!supported)
        {
            LogError(R"([{}] {}: The elements of node #{} ({}) cannot be edited on this game version.)",
                     ExtensionName, aSectorMod.mod, aIndex, aType.ToString());
            return false;
        }
        if (count != aExpected)
        {
            LogError(R"([{}] {}: The target node #{} has {} element(s), but the mod expects {}.)",
                     ExtensionName, aSectorMod.mod, aIndex, count, aExpected);
            return false;
        }
        return true;
    };

    bool nodeValidationPassed = true;

    for (const auto& nodeMutation : aSectorMod.nodeMutations)
    {
        Red::CompiledNodeInstanceSetupInfo* nodeSetup;
        Red::worldNode* nodeDefinition;
        if (!resolve(nodeMutation.nodeIndex, nodeMutation.nodeType, nodeSetup, nodeDefinition) ||
            (!nodeMutation.elementMutations.empty() &&
             !checkElements(nodeMutation.nodeIndex, nodeMutation.nodeType, nodeSetup, nodeDefinition, nodeMutation.expectedElements)))
        {
            nodeValidationPassed = false;
            continue;
        }
        for (const auto& elementMutation : nodeMutation.elementMutations)
        {
            if (elementMutation.elementIndex < 0 || elementMutation.elementIndex >= nodeMutation.expectedElements)
            {
                LogError(R"([{}] {}: The target node #{} has no element #{}.)", ExtensionName, aSectorMod.mod,
                         nodeMutation.nodeIndex, elementMutation.elementIndex);
                nodeValidationPassed = false;
            }
        }
    }

    for (const auto& nodeDeletion : aSectorMod.nodeDeletions)
    {
        Red::CompiledNodeInstanceSetupInfo* nodeSetup;
        Red::worldNode* nodeDefinition;
        if (!resolve(nodeDeletion.nodeIndex, nodeDeletion.nodeType, nodeSetup, nodeDefinition) ||
            (!nodeDeletion.elementDeletions.empty() &&
             !checkElements(nodeDeletion.nodeIndex, nodeDeletion.nodeType, nodeSetup, nodeDefinition, nodeDeletion.expectedElements)))
        {
            nodeValidationPassed = false;
            continue;
        }
        if (nodeDeletion.nodeType == CookedCollisionNodeType)
        {
            CollisionRun run{};
            GetCollisionRun(nodeDefinition, nodeSetup->instanceIndex, run);
            for (const auto& elementDeletion : nodeDeletion.elementDeletions)
            {
                if (elementDeletion.elementIndex < 0 || elementDeletion.elementIndex >= static_cast<int64_t>(run.numActors) ||
                    (elementDeletion.subElementIndex >= 0 &&
                     elementDeletion.subElementIndex >= run.actors[elementDeletion.elementIndex].numShapes))
                {
                    LogError(R"([{}] {}: The target node #{} has no actor #{} shape #{}.)", ExtensionName, aSectorMod.mod,
                             nodeDeletion.nodeIndex, elementDeletion.elementIndex, elementDeletion.subElementIndex);
                    nodeValidationPassed = false;
                }
            }
        }
        else
        {
            for (const auto& elementDeletion : nodeDeletion.elementDeletions)
            {
                if (elementDeletion.elementIndex < 0 || elementDeletion.elementIndex >= nodeDeletion.expectedElements)
                {
                    LogError(R"([{}] {}: The target node #{} has no element #{}.)", ExtensionName, aSectorMod.mod,
                             nodeDeletion.nodeIndex, elementDeletion.elementIndex);
                    nodeValidationPassed = false;
                }
            }
        }
    }

    if (!nodeValidationPassed)
    {
        return false;
    }

    for (const auto& nodeMutation : aSectorMod.nodeMutations)
    {
        auto* nodeSetup = buffer.nodeSetups.GetInstance(nodeMutation.nodeIndex);
        auto* nodeDefinition = buffer.nodes[nodeSetup->GetNodeIndex()].instance;

        if (nodeMutation.modifyPosition)
        {
            nodeSetup->transform.position = nodeMutation.position;
        }

        if (nodeMutation.modifyOrientation)
        {
            nodeSetup->transform.orientation = nodeMutation.orientation;
        }

        if (nodeMutation.modifyScale)
        {
            nodeSetup->scale = nodeMutation.scale;
        }

        if (nodeMutation.modifyResource)
        {
            if (nodeMutation.nodeType == InstancedMeshNodeType)
            {
                auto& entries = Raw::InstancedMeshNode::Entries::Ref(nodeDefinition);
                if (nodeSetup->instanceIndex < entries.Size())
                    entries.Data()[nodeSetup->instanceIndex].mesh = nodeMutation.resourcePath;
            }
            else if (IsCooked(nodeDefinition))
            {
                LogWarning(R"([{}] {}: The resource of cooked node #{} cannot be changed yet.)",
                           ExtensionName, aSectorMod.mod, nodeMutation.nodeIndex);
            }
            else if (auto* meshDefinition = Red::Cast<Red::worldMeshNode>(nodeDefinition))
            {
                meshDefinition->mesh.path = nodeMutation.resourcePath;
            }
            else if (auto* entityDefinition = Red::Cast<Red::worldEntityNode>(nodeDefinition))
            {
                entityDefinition->entityTemplate.path = nodeMutation.resourcePath;
            }
            else if (auto* effectDefinition = Red::Cast<Red::worldEffectNode>(nodeDefinition))
            {
                effectDefinition->effect.path = nodeMutation.resourcePath;
            }
            else if (auto* decalDefinition = Red::Cast<Red::worldStaticDecalNode>(nodeDefinition))
            {
                decalDefinition->material.path = nodeMutation.resourcePath;
            }
            else if (auto* physicalDefinition = Red::Cast<Red::worldPhysicalDestructionNode>(nodeDefinition))
            {
                physicalDefinition->mesh.path = nodeMutation.resourcePath;
            }
            else if (auto* staticOccluderDefinition = Red::Cast<Red::worldStaticOccluderMeshNode>(nodeDefinition))
            {
                staticOccluderDefinition->mesh.path = nodeMutation.resourcePath;
            }
            else if (auto* instancedOccluderDefinition = Red::Cast<Red::worldInstancedOccluderNode>(nodeDefinition))
            {
                instancedOccluderDefinition->mesh.path = nodeMutation.resourcePath;
            }
            else if (auto* foliageDefinition = Red::Cast<Red::worldFoliageNode>(nodeDefinition))
            {
                foliageDefinition->mesh.path = nodeMutation.resourcePath;
            }
            else if (auto* terrainDefinition = Red::Cast<Red::worldTerrainMeshNode>(nodeDefinition))
            {
                terrainDefinition->meshRef.path = nodeMutation.resourcePath;
            }
        }

        if (nodeMutation.modifyAppearance)
        {
            if (nodeMutation.nodeType == InstancedMeshNodeType)
            {
                auto& entries = Raw::InstancedMeshNode::Entries::Ref(nodeDefinition);
                if (nodeSetup->instanceIndex < entries.Size())
                    entries.Data()[nodeSetup->instanceIndex].meshAppearance = nodeMutation.appearanceName;
            }
            else if (IsCooked(nodeDefinition))
            {
                LogWarning(R"([{}] {}: The appearance of cooked node #{} cannot be changed yet.)",
                           ExtensionName, aSectorMod.mod, nodeMutation.nodeIndex);
            }
            else if (auto* meshDefinition = Red::Cast<Red::worldMeshNode>(nodeDefinition))
            {
                meshDefinition->meshAppearance = nodeMutation.appearanceName;
            }
            else if (auto* entityDefinition = Red::Cast<Red::worldEntityNode>(nodeDefinition))
            {
                entityDefinition->appearanceName = nodeMutation.appearanceName;
            }
            else if (auto* spawnerDefinition = Red::Cast<Red::worldPopulationSpawnerNode>(nodeDefinition))
            {
                spawnerDefinition->appearanceName = nodeMutation.appearanceName;
            }
            else if (auto* physicalDefinition = Red::Cast<Red::worldPhysicalDestructionNode>(nodeDefinition))
            {
                physicalDefinition->meshAppearance = nodeMutation.appearanceName;
            }
            else if (auto* foliageDefinition = Red::Cast<Red::worldFoliageNode>(nodeDefinition))
            {
                foliageDefinition->meshAppearance = nodeMutation.appearanceName;
            }
        }

        if (nodeMutation.modifyRecordID)
        {
            if (auto* spawnerDefinition = Red::Cast<Red::worldPopulationSpawnerNode>(nodeDefinition))
            {
                spawnerDefinition->objectRecordId = nodeMutation.recordID;
            }
        }

        if (nodeMutation.modifyProxyNodes && nodeMutation.nbNodesUnderProxyDiff > 0)
        {
            if (auto* proxyDefinition = Red::Cast<Red::worldPrefabProxyMeshNode>(nodeDefinition))
            {
                proxyDefinition->nbNodesUnderProxy += nodeMutation.nbNodesUnderProxyDiff;
            }
        }

        if (nodeMutation.elementMutations.empty())
            continue;

        if (nodeMutation.nodeType == CookedCollisionNodeType)
        {
            CollisionRun run{};
            GetCollisionRun(nodeDefinition, nodeSetup->instanceIndex, run);
            for (const auto& elementMutation : nodeMutation.elementMutations)
            {
                auto* actor = run.actors + elementMutation.elementIndex;

                if (elementMutation.modifyPosition)
                {
                    actor->transform.Position = elementMutation.position;
                }

                if (elementMutation.modifyOrientation)
                {
                    actor->transform.Orientation = elementMutation.orientation;
                }
            }
        }
        else if (nodeMutation.nodeType == InstancedMeshNodeType)
        {
            uint32_t count = 0;
            auto* instances = GetInstanceRun(nodeDefinition, nodeSetup->instanceIndex, count);
            for (const auto& elementMutation : nodeMutation.elementMutations)
            {
                auto* instance = instances + elementMutation.elementIndex;

                if (elementMutation.modifyPosition)
                {
                    instance->transform.Position = elementMutation.position;
                }

                if (elementMutation.modifyOrientation)
                {
                    instance->transform.Orientation = elementMutation.orientation;
                }

                if (elementMutation.modifyScale)
                {
                    instance->scale = elementMutation.scale;
                }
            }
        }
        else if (nodeMutation.nodeType == InstancedDestructibleNodeType)
        {
            auto& instances = Raw::InstancedDestructibleMeshNode::InstanceTransforms::Ref(nodeDefinition);
            auto inverseTransform = nodeSetup->transform.Inverse();
            for (const auto& elementMutation : nodeMutation.elementMutations)
            {
                auto* instance = instances.Data() + elementMutation.elementIndex;
                auto newTransform = nodeSetup->transform;

                if (elementMutation.modifyPosition)
                {
                    newTransform.position = elementMutation.position;
                }

                if (elementMutation.modifyOrientation)
                {
                    newTransform.orientation = elementMutation.orientation;
                }

                *instance = newTransform * inverseTransform;
            }
        }
    }

    for (const auto& nodeDeletion : aSectorMod.nodeDeletions)
    {
        auto* nodeSetup = buffer.nodeSetups.GetInstance(nodeDeletion.nodeIndex);
        auto* nodeDefinition = buffer.nodes[nodeSetup->GetNodeIndex()].instance;

        if (nodeDeletion.nodeType == CookedCollisionNodeType)
        {
            CollisionRun run{};
            if (!GetCollisionRun(nodeDefinition, nodeSetup->instanceIndex, run))
                continue;

            if (nodeDeletion.elementDeletions.empty())
            {
                for (uint32_t i = 0; i < run.numActors; ++i)
                    run.actors[i].transform.Position.z.Bits = DelZ;
                continue;
            }

            // A shape is switched off with the "No Collision" preset when its run lists one: the
            // presets of a cooked run cannot grow.
            int32_t noCollisionIndex = -1;
            for (uint32_t i = 0; i < run.numPresets; ++i)
            {
                if (run.presets[i] == NoCollisionPreset)
                {
                    noCollisionIndex = static_cast<int32_t>(i);
                    break;
                }
            }

            for (const auto& elementDeletion : nodeDeletion.elementDeletions)
            {
                auto& actor = run.actors[elementDeletion.elementIndex];
                if (elementDeletion.subElementIndex >= 0 && actor.numShapes > 1)
                {
                    if (noCollisionIndex < 0)
                    {
                        LogWarning(R"([{}] {}: Node #{} actor #{} shape #{} cannot be switched off: its collision lists no "No Collision" preset.)",
                                   ExtensionName, aSectorMod.mod, nodeDeletion.nodeIndex,
                                   elementDeletion.elementIndex, elementDeletion.subElementIndex);
                        continue;
                    }
                    run.shapes[actor.shapeStartIndex + elementDeletion.subElementIndex].presetIndex =
                        static_cast<uint8_t>(noCollisionIndex);
                }
                else
                {
                    actor.transform.Position.z.Bits = DelZ;
                }
            }
            continue;
        }

        if (nodeDeletion.nodeType == InstancedMeshNodeType)
        {
            uint32_t count = 0;
            auto* instances = GetInstanceRun(nodeDefinition, nodeSetup->instanceIndex, count);
            if (!instances)
                continue;

            if (nodeDeletion.elementDeletions.empty())
            {
                for (uint32_t i = 0; i < count; ++i)
                    instances[i].scale = {0, 0, 0};
            }
            else
            {
                for (const auto& elementDeletion : nodeDeletion.elementDeletions)
                    instances[elementDeletion.elementIndex].scale = {0, 0, 0};
            }
            continue;
        }

        if (nodeDeletion.nodeType == InstancedDestructibleNodeType && !nodeDeletion.elementDeletions.empty())
        {
            auto& instances = Raw::InstancedDestructibleMeshNode::InstanceTransforms::Ref(nodeDefinition);
            for (const auto& elementDeletion : nodeDeletion.elementDeletions)
                instances.Data()[elementDeletion.elementIndex].position.Z = -2000;
            continue;
        }

        if (nodeDeletion.nodeType == StaticMeshNodeType || nodeDeletion.nodeType == CookedStaticMeshNodeType)
        {
            nodeSetup->scale.X = 0;
            nodeSetup->scale.Y = 0;
            nodeSetup->scale.Z = 0;
            continue;
        }

        if (nodeDeletion.nodeType == AdvertisementNodeType)
        {
            nodeSetup->scale.X = 0;
            nodeSetup->scale.Y = 0;
            nodeSetup->scale.Z = 0;
            nodeSetup->node = s_dummyNode.instance;
            nodeSetup->instanceIndex = 0xFFFFFFFF;
            continue;
        }

        nodeSetup->transform.position.Z = -2000;
        nodeSetup->scale.X = 0;
        nodeSetup->scale.Y = 0;
        nodeSetup->scale.Z = 0;

        nodeSetup->globalNodeID = 0;
        // nodeSetup->proxyNodeID = 0;

        // The dummy is an uncooked node: the setup must not keep the element index of the node it
        // replaces. 2.5 indexes the placements of more classes than worldCooked* (instanced
        // destructibles, physical destruction...); a kept index makes the game read the dummy as a
        // cooked body and fault (psiberx/cp2077-archive-xl#36).
        nodeSetup->node = s_dummyNode.instance;
        nodeSetup->instanceIndex = 0xFFFFFFFF;
    }

    return true;
}

void App::WorldStreamingExtension::OnRegisterSpots(Red::AIWorkspotManager* aManager,
                                                   const Red::DynArray<Red::AISpotPersistentData>& aNewSpots)
{
    auto allSpots = Raw::AIWorkspotManager::Spots::Ptr(aManager);
    auto merging = (allSpots->size != 0);

    if (merging)
    {
        auto newSize = allSpots->size + aNewSpots.Size();
        if (allSpots->capacity < newSize)
        {
            Raw::AISpotPersistentDataArray::Reserve(allSpots, newSize);
        }

        allSpots->flags |= static_cast<int32_t>(Red::SortedArray<Red::AISpotPersistentData>::Flags::NotSorted);
    }

    Raw::AIWorkspotManager::RegisterSpots(aManager, aNewSpots);
}
