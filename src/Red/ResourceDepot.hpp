#pragma once

namespace Red
{
// 2.5 renumbered the archive scopes: the depot looks the mod group up with 6 (archive\pc\mod and
// mods\*\archives, rva 0x1259754 and 0x12594ac), the patch group with 4. The SDK enum still says Mod = 4.
constexpr auto ModArchiveScope = static_cast<ArchiveScope>(6);
}

namespace Raw::ResourceDepot
{
constexpr auto InitializeArchives = Core::RawFunc<
    /* addr = */ Red::AddressLib::ResourceDepot_InitializeArchives,
    /* type = */ void (*)(Red::ResourceDepot* aDepot)>{};

constexpr auto LoadArchives = Core::RawFunc<
    /* addr = */ Red::AddressLib::ResourceDepot_LoadArchives,
    /* type = */ void (*)(Red::ResourceDepot* aDepot,
                          Red::ArchiveGroup& aGroup,
                          const Red::DynArray<Red::CString>& aArchivePaths,
                          Red::DynArray<Red::ResourcePath>& aLoadedResourcePaths,
                          bool aMemoryResident)>{};

constexpr auto RequestResource = Core::RawFunc<
    /* addr = */ Red::AddressLib::ResourceDepot_RequestResource,
    /* type = */ uintptr_t* (*)(Red::ResourceDepot* aDepot,
                                const uintptr_t* aOutResourceHandle,
                                Red::ResourcePath aPath,
                                const int32_t* aArchiveHandle)>{};

constexpr auto CheckResource = Core::RawFunc<
    /* addr = */ Red::AddressLib::ResourceDepot_CheckResource,
    /* type = */ bool (*)(Red::ResourceDepot* aDepot, Red::ResourcePath aPath)>{};
}
