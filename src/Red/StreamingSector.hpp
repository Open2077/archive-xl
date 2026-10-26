#pragma once

#include "Red/Common.hpp"

namespace Red
{
// Patch 2.5 (sectors version 68): the node index, flags, streaming priority and time-of-day
// visibility are packed in one word (the node buffer's post-load reads `dword [setup + 0x70] &
// 0x7FFFF`), and a placement of a cooked node (worldCooked*, worldInstancedMeshNode) picks the
// node's entry by its instance index.
struct __declspec(align(0x10)) CompiledNodeInstanceSetupInfo
{
    Transform transform;                  // 00
    Vector3 scale;                        // 20
    Vector3 secondaryRefPointPosition;    // 2C
    Vector3 streamingRefPoint;            // 38
    Vector3 preboostStreamingRefPoint;    // 44
    worldNode* node;                      // 50
    uint64_t globalNodeID;                // 58
    uint64_t proxyNodeID;                 // 60
    ResourcePath externalInplaceResource; // 68
    uint64_t packed;                      // 70 - nodeIndex:19, flags:21, priority:8, todvisMask:16
    float secondaryRefPointDistance;      // 78
    float streamingDistance;              // 7C
    uint32_t instanceIndex;               // 80 - 0xFFFFFFFF on uncooked nodes
    uint8_t dataGroup;                    // 84
    uint8_t unk85[0x90 - 0x85];           // 85

    [[nodiscard]] inline uint32_t GetNodeIndex() const
    {
        return static_cast<uint32_t>(packed & 0x7FFFF);
    }
};
RED4EXT_ASSERT_SIZE(CompiledNodeInstanceSetupInfo, 0x90);
RED4EXT_ASSERT_OFFSET(CompiledNodeInstanceSetupInfo, node, 0x50);
RED4EXT_ASSERT_OFFSET(CompiledNodeInstanceSetupInfo, globalNodeID, 0x58);
RED4EXT_ASSERT_OFFSET(CompiledNodeInstanceSetupInfo, packed, 0x70);
RED4EXT_ASSERT_OFFSET(CompiledNodeInstanceSetupInfo, instanceIndex, 0x80);

struct CompiledNodeInstanceSetupInfoBuffer : DataBuffer
{
    [[nodiscard]] inline CompiledNodeInstanceSetupInfo* begin() const
    {
        return reinterpret_cast<CompiledNodeInstanceSetupInfo*>(buffer.data);
    }

    [[nodiscard]] inline CompiledNodeInstanceSetupInfo* end() const
    {
        return reinterpret_cast<CompiledNodeInstanceSetupInfo*>(reinterpret_cast<uintptr_t>(buffer.data) +
                                                                buffer.size);
    }

    [[nodiscard]] CompiledNodeInstanceSetupInfo* GetInstance(int64_t aIndex) const
    {
        return begin() + aIndex;
    }

    uint32_t GetInstanceCount()
    {
        return buffer.size / sizeof(CompiledNodeInstanceSetupInfo);
    }

    uint64_t unk28; // 28 - 2.5
};
RED4EXT_ASSERT_SIZE(CompiledNodeInstanceSetupInfoBuffer, 0x30);

struct StreamingSectorNodeBuffer
{
    CompiledNodeInstanceSetupInfoBuffer nodeSetups; // 00
    DynArray<Handle<worldNode>> nodes;              // 30
    DynArray<NodeRef> nodeRefs;                     // 40
};
RED4EXT_ASSERT_OFFSET(StreamingSectorNodeBuffer, nodes, 0x30);
RED4EXT_ASSERT_OFFSET(StreamingSectorNodeBuffer, nodeRefs, 0x40);

// A cooked collision node keeps, per placement, the counts of the 2.31 worldCollisionNode and the
// offset of its run of the node's compiled data, which keeps the 2.31 layout: actors (0x30), shape
// infos (0x20), positions, rotations, scales, materials, presets, shape indices, material indices.
struct CookedCollisionEntry
{
    float extents[4];            // 00
    uint64_t sectorHash;         // 10
    uint32_t dataOffset;         // 18
    uint16_t numActors;          // 1C
    uint16_t numShapeInfos;      // 1E
    uint16_t numShapePositions;  // 20
    uint16_t numShapeRotations;  // 22
    uint16_t numScales;          // 24
    uint16_t numMaterials;       // 26
    uint16_t numPresets;         // 28
    uint16_t numShapeIndices;    // 2A
    uint16_t numMaterialIndices; // 2C
    uint8_t lod;                 // 2E
    uint8_t unk2F;               // 2F
};
RED4EXT_ASSERT_SIZE(CookedCollisionEntry, 0x30);

// A cooked instanced mesh node: per placement an entry (mesh, appearance...) and a range of the
// node's world transforms.
struct InstancedMeshEntry
{
    Box bounds;                // 00
    ResourcePath mesh;         // 20
    CName meshAppearance;      // 28
    uint8_t unk30[0x40 - 0x30]; // 30
};
RED4EXT_ASSERT_SIZE(InstancedMeshEntry, 0x40);

struct InstancedMeshRange
{
    uint16_t startIndex; // 00
    uint16_t numElements; // 02
};
RED4EXT_ASSERT_SIZE(InstancedMeshRange, 0x4);

struct CollisionActor
{
    WorldTransform transform;   // 00
    uint16_t shapeStartIndex;   // 20
    uint16_t numShapes;         // 22
    int16_t scaleIndex;         // 24
    uint8_t unk26[0x30 - 0x26]; // 26
};
RED4EXT_ASSERT_SIZE(CollisionActor, 0x30);

struct CollisionShape
{
    physicsGeometryKey geometryKey; // 00
    uint16_t unk10;                 // 10
    uint16_t unk12;                 // 12
    int16_t positionIndex;          // 14
    int16_t rotationIndex;          // 16
    uint8_t presetIndex;            // 18
    uint8_t unk19[0x20 - 0x19];     // 19
};
RED4EXT_ASSERT_SIZE(CollisionShape, 0x20);

struct TransformBuffer : DataBuffer
{
    [[nodiscard]] inline Transform* begin() const
    {
        return reinterpret_cast<Transform*>(buffer.data);
    }

    [[nodiscard]] inline Transform* end() const
    {
        return reinterpret_cast<Transform*>(reinterpret_cast<uintptr_t>(buffer.data) + buffer.size);
    }

    [[nodiscard]] Transform* Get(int64_t aIndex) const
    {
        return begin() + aIndex;
    }

    uint32_t GetCount()
    {
        return buffer.size / sizeof(Transform);
    }
};
RED4EXT_ASSERT_SIZE(TransformBuffer, 0x28);

struct WorldTransformBuffer : DataBuffer
{
    [[nodiscard]] inline worldNodeTransform* begin() const
    {
        return reinterpret_cast<worldNodeTransform*>(buffer.data);
    }

    [[nodiscard]] inline worldNodeTransform* end() const
    {
        return reinterpret_cast<worldNodeTransform*>(reinterpret_cast<uintptr_t>(buffer.data) + buffer.size);
    }

    [[nodiscard]] worldNodeTransform* Get(int64_t aIndex) const
    {
        return begin() + aIndex;
    }

    uint32_t GetCount()
    {
        return buffer.size / sizeof(worldNodeTransform);
    }
};
RED4EXT_ASSERT_SIZE(WorldTransformBuffer, 0x28);

struct RenderProxyTransform
{
    WorldTransform transform;
    Vector3 scale;
};

struct RenderProxyTransformData : DataBuffer
{
    [[nodiscard]] inline RenderProxyTransform* begin() const
    {
        return reinterpret_cast<RenderProxyTransform*>(buffer.data);
    }

    [[nodiscard]] inline RenderProxyTransform* end() const
    {
        return reinterpret_cast<RenderProxyTransform*>(reinterpret_cast<uintptr_t>(buffer.data) + buffer.size);
    }

    [[nodiscard]] RenderProxyTransform* Get(int64_t aIndex) const
    {
        return begin() + aIndex;
    }

    uint32_t GetCount()
    {
        return buffer.size / sizeof(RenderProxyTransform);
    }
};
RED4EXT_ASSERT_SIZE(RenderProxyTransformData, 0x28);
}

namespace Raw::StreamingSector
{
using NodeBuffer = Core::OffsetPtr<0x40, Red::StreamingSectorNodeBuffer>;

constexpr auto PostLoad = Core::RawFunc<
    /* addr = */ Red::AddressLib::StreamingSector_PostLoad,
    /* type = */ void (*)(Red::worldStreamingSector* aSector, uint64_t a2)>();
}

// 2.5 reorganized the uncooked worldCollisionNode (0x120 -> 0xC0, compiled data at 0x60): its
// actors are not addressed. The game's collision is cooked.
namespace Raw::CookedCollisionNode
{
// Read by the node's entry bounds virtual: `[this + 0x40] + index * dword [this + 0x50]`.
using Entries = Core::OffsetPtr<0x40, Red::DynArray<uint8_t>>;
using EntryStride = Core::OffsetPtr<0x50, uint32_t>;
using CompiledData = Core::OffsetPtr<0x58, Red::DataBuffer>;
}

namespace Raw::InstancedMeshNode
{
// Serialized by the node: version at 0x90, then the arrays at 0x60, 0x70 (entries, 0x40 each) and 0x80.
using Transforms = Core::OffsetPtr<0x38, Red::RenderProxyTransformData>;
using Entries = Core::OffsetPtr<0x70, Red::DynArray<Red::InstancedMeshEntry>>;
using Ranges = Core::OffsetPtr<0x80, Red::DynArray<Red::InstancedMeshRange>>;
}

namespace Raw::InstancedDestructibleMeshNode
{
// 2.5: the instances' transforms (relative to the placement) are the node's own array.
using InstanceTransforms = Core::OffsetPtr<0xB8, Red::DynArray<Red::Transform>>;
}
