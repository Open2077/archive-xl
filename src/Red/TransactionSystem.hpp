#pragma once

#include <intrin.h>

// namespace Raw::TransactionSystem
// {
// constexpr auto IsSlotSpawning = Core::RawFunc<
//     /* addr = */ Red::AddressLib::TransactionSystem_IsSlotSpawning,
//     /* type = */ bool (*)(Red::IScriptable* aSystem, Red::IScriptable* aOwner, Red::TweakDBID aSlotID)>();
// }

namespace Raw::TransactionSystem
{
// The per-owner attachment slots: FindSlotData (vtable 0x448) gets this holder, takes the
// spinlock at +0x100 shared and scans the DynArray at +0xB0 (0x90-byte AttachmentSlotData).
constexpr auto GetSlotHolder = Core::RawFunc<
    /* addr = */ Red::AddressLib::TransactionSystem_GetSlotHolder,
    /* type = */ void (*)(Red::ITransactionSystem* aSystem, Red::Handle<Red::ISerializable>& aOut,
                          Red::IScriptable* aOwner)>();

using SlotHolderLock = Core::OffsetPtr<0x100, volatile uint8_t>;
using SlotHolderSlots = Core::OffsetPtr<0xB0, Red::DynArray<Red::AttachmentSlotData>>;

constexpr uint8_t SlotHolderWriteLocked = 0xFF;
}

namespace Red
{
// FindSlotData that survives being called from inside the game's own slot update.
//
// On 2.5 the game resolves appearance suffixes (AppearanceChanger::GetSuffixes, then
// GetSuffixValue) while it holds the holder's slot lock exclusively. The lock is a
// non-reentrant reader/writer spinlock, so ITransactionSystem::FindSlotData from a suffix
// hook spun forever on the game thread until the engine watchdog closed the game. When the
// lock stays write-held past a short wait, the caller is inside that update and the slots
// are read the way the writer itself sees them, without the lock.
template<typename Predicate>
inline AttachmentSlotData* FindSlotDataReentrant(ITransactionSystem* aSystem, IScriptable* aOwner,
                                                 Predicate&& aPredicate)
{
    if (!aSystem || !aOwner)
        return nullptr;

    Handle<ISerializable> holder;
    Raw::TransactionSystem::GetSlotHolder(aSystem, holder, aOwner);
    if (!holder)
        return nullptr;

    auto& lock = Raw::TransactionSystem::SlotHolderLock::Ref(holder.instance);
    for (auto spin = 0; lock == Raw::TransactionSystem::SlotHolderWriteLocked; ++spin)
    {
        if (spin == 4096)
        {
            for (auto& slotData : Raw::TransactionSystem::SlotHolderSlots::Ref(holder.instance))
            {
                if (aPredicate(slotData))
                    return &slotData;
            }
            return nullptr;
        }
        _mm_pause();
    }

    return aSystem->FindSlotData(aOwner, std::forward<Predicate>(aPredicate));
}
}
