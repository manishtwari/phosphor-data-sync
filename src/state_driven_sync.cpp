// SPDX-License-Identifier: Apache-2.0

#include "state_driven_sync.hpp"

#include "manager.hpp"

#include <iostream>

namespace data_sync::state_driven
{

StateDrivenSync::StateDrivenSync(sdbusplus::async::context& ctx,
                                 data_sync::Manager& manager) :
    _ctx(ctx), _manager(manager)
{}

sdbusplus::async::task<SubTreeType>
    // NOLINTNEXTLINE
    StateDrivenSync::getSubTree(const std::string& interface)
{
    auto interfaceList = {interface};

    using ObjectMapper =
        sdbusplus::client::xyz::openbmc_project::ObjectMapper<>;

    auto mapper = ObjectMapper(_ctx)
                      .service(ObjectMapper::default_service)
                      .path(ObjectMapper::instance_path);

    co_return co_await mapper.get_sub_tree("/", 0, interfaceList);
}

sdbusplus::async::task<PropertiesMap>
    // NOLINTNEXTLINE
    StateDrivenSync::getAllProperties(const std::string& service,
                                      const std::string& objectPath,
                                      const std::string& interface)
{
    auto watcher = sdbusplus::async::proxy()
                       .service(service)
                       .path(objectPath)
                       .interface(interface);

    co_return co_await watcher.get_all_properties<BMCProperties>(_ctx);
}

sdbusplus::async::task<>
    // NOLINTNEXTLINE
    processStates(const auto& properties, const auto& expectedPropertyStates,
                  std::function<sdbusplus::async::task<>()> onStateMatch)
{
    try
    {
        for (const auto& [property, expectedValues] : expectedPropertyStates)
        {
            auto it = properties.find(property);
            if (it == properties.end())
            {
                lg2::debug("Property '{PROPERTY}' not found in current state",
                           "PROPERTY", property);
                continue;
            }

            const auto& variant = it->second;

            const std::string* valuePtr = std::get_if<std::string>(&variant);
            if (!valuePtr)
            {
                lg2::error("Property '{PROPERTY}' is not of type string",
                           "PROPERTY", property);
                continue;
            }

            const std::string& value = *valuePtr;
            lg2::debug("Property '{PROPERTY}' has value '{VALUE}'", "PROPERTY",
                       property, "VALUE", value);

            if (!value.empty() && expectedValues.count(value))
            {
                lg2::info(
                    "Matched property '{PROPERTY}' with value '{VALUE}', triggering onStateMatch",
                    "PROPERTY", property, "VALUE", value);
                co_await onStateMatch();
            }
        }

        co_return;
    }
    catch (const sdbusplus::exception::SdBusError& e)
    {
        lg2::error("processStates failed: {ERROR}", "ERROR", e.what());
        co_return;
    }
}

// NOLINTNEXTLINE
sdbusplus::async::task<> StateDrivenSync::updateSyncStateBasedOnProps(
    const PropertiesMap& properties, const std::string& interface)
{
    try
    {
        auto it = _manager.watcherLists.find(interface);
        if (it == _manager.watcherLists.end() || it->second.empty())
        {
            lg2::debug("No configs found for interface '{INTERFACE}'",
                       "INTERFACE", interface);
            co_return;
        }

        for (auto* cfg : it->second)
        {
            auto& stateSync = cfg->_stateDrivenSync.value();
            auto& interfaceInfo = cfg->_stateDrivenSync->_interfaces[interface];

            // Handle Suspend
            co_await processStates(properties, interfaceInfo._suspendStates,
                                   // NOLINTNEXTLINE
                                   [&]() -> sdbusplus::async::task<> {
                if (!stateSync._suspendSync)
                {
                    stateSync._suspendSync = true;
                    lg2::info("Sync suspended on interface '{INTERFACE}'",
                              "INTERFACE", interface);
                }
                co_return;
            });

            // Handle Resume
            // NOLINTNEXTLINE
            co_await processStates(properties, interfaceInfo._resumeStates,
                                   // NOLINTNEXTLINE
                                   [this, cfg, &stateSync,
                                    &interface]() -> sdbusplus::async::task<> {
                if (stateSync._suspendSync)
                {
                    stateSync._suspendSync = false;
                    bool syncSuccess = co_await _manager.syncCallback(*cfg);
                    if (syncSuccess)
                    {
                        lg2::info(
                            "Resumed and synced successfully on interface '{INTERFACE}'",
                            "INTERFACE", interface);
                    }
                    else
                    {
                        lg2::error(
                            "Resumed but sync failed on interface '{INTERFACE}'",
                            "INTERFACE", interface);
                    }
                }
                co_return;
            });
        }
    }
    catch (const sdbusplus::exception::SdBusError& e)
    {
        lg2::error(
            "Failed to update sync state from props on interface '{INTERFACE}': {ERROR}",
            "INTERFACE", interface, "ERROR", e.what());
    }

    co_return;
}

sdbusplus::async::task<std::string>
    // NOLINTNEXTLINE
    StateDrivenSync::waitUntilIFaceAvailable(const std::string& interface,
                                             const std::string& service)
{
    sdbusplus::async::match nameMatch(_ctx, rules::nameOwnerChanged(service));

    lg2::debug("Watching service '{SERVICE}' for interface '{INTERFACE}'",
               "SERVICE", service, "INTERFACE", interface);

    while (!_ctx.stop_requested())
    {
        auto [receivedService, oldOwner, newOwner] =
            co_await nameMatch.next<std::string, std::string, std::string>();

        if (receivedService != service)
        {
            continue;
        }

        if (!newOwner.empty())
        {
            // Retry is necessary here because receiving the NameOwnerChanged
            // signal only confirms that the service is registered on the bus
            // However, the corresponding interface and object path may not be
            // immediately available. There is a slight delay before the service
            // emits the signal to register its interfaces, so we retry a few
            // times until the interface appears in the ObjectMapper
            for (int retry = 0; retry < 5; ++retry)
            {
                auto subtree = co_await getSubTree(interface);

                auto objectPath = co_await extractObjPathFromSubtree(
                    subtree, service, interface);

                if (!objectPath.empty())
                {
                    lg2::debug(
                        "Interface '{INTERFACE}' available at path '{PATH}'",
                        "INTERFACE", interface, "PATH", objectPath);
                    co_return objectPath;
                }

                co_await sdbusplus::async::sleep_for(
                    _ctx, std::chrono::milliseconds(30));
            }

            lg2::debug(
                "Interface '{INTERFACE}' not ready after retries. Waiting for next service change.",
                "INTERFACE", interface);
        }
    }

    co_return std::string{};
}

sdbusplus::async::task<>
    // NOLINTNEXTLINE
    StateDrivenSync::updateSuspendFlag(const std::string& interface)
{
    auto it = _manager.watcherLists.find(interface);
    auto& configs = it->second;

    for (auto* cfg : configs)
    {
        auto& stateSync = cfg->_stateDrivenSync.value();
        if (stateSync._suspendSync)
        {
            stateSync._suspendSync = false;

            bool success = co_await _manager.syncCallback(*cfg);
            if (success)
            {
                lg2::info(
                    "Sync resumed and succeeded on interface '{INTERFACE}'",
                    "INTERFACE", interface);
            }
            else
            {
                lg2::error("Sync resumed but failed on interface '{INTERFACE}'",
                           "INTERFACE", interface);
            }
            co_return;
        }
    }

    lg2::debug("No suspended sync flag to update for interface '{INTERFACE}'",
               "INTERFACE", interface);
    co_return;
}

sdbusplus::async::task<>
    // NOLINTNEXTLINE
    StateDrivenSync::monitorServiceAvailability(std::string service,
                                                std::string interface)
{
    while (!_ctx.stop_requested())
    {
        sdbusplus::async::match nameMatch(_ctx,
                                          rules::nameOwnerChanged(service));

        auto [receivedService, oldOwner, newOwner] =
            co_await nameMatch.next<std::string, std::string, std::string>();

        if (receivedService != service)
        {
            continue;
        }

        if (newOwner.empty())
        {
            lg2::info(
                "Service '{SERVICE}' disappeared. Monitoring for recovery...",
                "SERVICE", service);

            co_await updateSuspendFlag(interface);

            auto restoredPath = co_await waitUntilIFaceAvailable(interface,
                                                                 service);

            if (!restoredPath.empty())
            {
                lg2::info(
                    "Service '{SERVICE}' and interface '{INTERFACE}' restored. Syncing state...",
                    "SERVICE", service, "INTERFACE", interface);

                auto props = co_await getAllProperties(service, restoredPath,
                                                       interface);
                co_await updateSyncStateBasedOnProps(props, interface);
            }
        }
    }

    co_return;
}

sdbusplus::async::task<std::string>
    // NOLINTNEXTLINE
    StateDrivenSync::extractObjPathFromSubtree(const auto& subtree,
                                               const std::string& service,
                                               const std::string& interface)
{
    for (const auto& [objectPath, serviceMap] : subtree)
    {
        auto it = serviceMap.find(service);
        if (it != serviceMap.end())
        {
            const auto& interfaces = it->second;
            if (std::find(interfaces.begin(), interfaces.end(), interface) !=
                interfaces.end())
            {
                co_return objectPath;
            }
        }
    }
    co_return std::string{};
}

sdbusplus::async::task<std::string>
    // NOLINTNEXTLINE
    StateDrivenSync::getServiceFromCfg(const auto& dataSyncCfgs)
{
    for (auto* cfg : dataSyncCfgs)
    {
        auto& stateSync = cfg->_stateDrivenSync.value();
        for (const auto& it : stateSync._interfaces)
        {
            auto& stateInfo = it.second;
            auto& serviceName = stateInfo._serviceName;
            co_return serviceName;
        }
    }
    co_return std::string{};
}

sdbusplus::async::task<>
    // NOLINTNEXTLINE
    StateDrivenSync::watchBmcPropertiesChanged(std::string Interface)
{
    auto itGroup = _manager.watcherLists.find(Interface);
    auto& dataSyncCfgs = itGroup->second;

    auto service = co_await getServiceFromCfg(dataSyncCfgs);

    auto subtree = co_await getSubTree(Interface);
    auto objectPath = co_await extractObjPathFromSubtree(subtree, service,
                                                         Interface);

    if (objectPath.empty())
    {
        objectPath = co_await waitUntilIFaceAvailable(Interface, service);
        lg2::info(
            "Service available and object path found for interface [{INTERFACE}]: {OBJPATH}",
            "INTERFACE", Interface, "OBJPATH", objectPath);
    }

    auto properties = co_await getAllProperties(service, objectPath, Interface);
    lg2::debug("Fetched properties for interface [{INTERFACE}]", "INTERFACE",
               Interface);

    try
    {
        co_await updateSyncStateBasedOnProps(properties, Interface);
    }
    catch (const sdbusplus::exception::SdBusError& e)
    {
        lg2::error(
            "Initial state sync failed for interface [{INTERFACE}]: {ERROR}",
            "INTERFACE", Interface, "ERROR", e.what());
    }

    _ctx.spawn(monitorServiceAvailability(service, Interface));

    sdbusplus::async::match match(
        _ctx, propChangeRule(service, Interface, objectPath));

    while (!_ctx.stop_requested())
    {
        auto [_,
              properties] = co_await match.next<std::string, PropertiesMap>();
        try
        {
            co_await updateSyncStateBasedOnProps(properties, Interface);
        }
        catch (const sdbusplus::exception::SdBusError& e)
        {
            lg2::error(
                "Failed to update sync state for interface [{INTERFACE}]: {ERROR}",
                "INTERFACE", Interface, "ERROR", e.what());
        }
    }

    co_return;
}

} // namespace data_sync::state_driven
