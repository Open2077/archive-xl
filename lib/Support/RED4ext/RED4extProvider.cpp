#include "RED4extProvider.hpp"

Support::RED4extProvider::RED4extProvider(RED4ext::v1::PluginHandle aPlugin, const RED4ext::v1::Sdk* aSdk) noexcept
    : m_plugin(aPlugin)
    , m_sdk(aSdk)
    , m_enableLogging(false)
    , m_enableHooking(false)
    , m_enableAddressLibrary(false)
{
}

void Support::RED4extProvider::OnInitialize()
{
    if (m_enableLogging)
    {
        LoggingDriver::SetDefault(*this);
    }

    if (m_enableHooking)
    {
        HookingDriver::SetDefault(*this);
    }

    if (m_enableAddressLibrary)
    {
        AddressResolver::SetDefault(*this);
    }
}

void Support::RED4extProvider::LogInfo(const std::string_view& aMessage)
{
    m_sdk->logger->Info(m_plugin, aMessage.data());
}

void Support::RED4extProvider::LogWarning(const std::string_view& aMessage)
{
    m_sdk->logger->Warn(m_plugin, aMessage.data());
}

void Support::RED4extProvider::LogError(const std::string_view& aMessage)
{
    m_sdk->logger->Error(m_plugin, aMessage.data());
}

void Support::RED4extProvider::LogDebug(const std::string_view& aMessage)
{
    m_sdk->logger->Debug(m_plugin, aMessage.data());
}

void Support::RED4extProvider::LogFlush()
{
}

bool Support::RED4extProvider::HookAttach(uintptr_t aAddress, void* aCallback)
{
    if (!aAddress)
        return false;

    return m_sdk->hooking->Attach(m_plugin, reinterpret_cast<void*>(aAddress), aCallback, nullptr);
}

bool Support::RED4extProvider::HookAttach(uintptr_t aAddress, void* aCallback, void** aOriginal)
{
    if (!aAddress)
        return false;

    return m_sdk->hooking->Attach(m_plugin, reinterpret_cast<void*>(aAddress), aCallback, aOriginal);
}

bool Support::RED4extProvider::HookDetach(uintptr_t aAddress)
{
    return m_sdk->hooking->Detach(m_plugin, reinterpret_cast<void*>(aAddress));
}

uintptr_t Support::RED4extProvider::ResolveAddress(uint32_t aAddressID)
{
    // A zero ID marks a function not located on this game version. Resolving
    // it would terminate the game; the hook fails instead and its module unloads.
    if (!aAddressID)
        return 0;

    return RED4ext::UniversalRelocBase::Resolve(aAddressID);
}
