// SPDX-License-Identifier: Apache-2.0

#include "state_driven_sync.hpp"

#include "manager.hpp"

#include <iostream>

namespace data_sync::state_driven
{

using SyncBMCData =
    sdbusplus::common::xyz::openbmc_project::control::SyncBMCData;

using Redundancy =
    sdbusplus::common::xyz::openbmc_project::state::bmc::Redundancy;

using Host = sdbusplus::common::xyz::openbmc_project::state::Host;

using BootProgress =
    sdbusplus::common::xyz::openbmc_project::state::boot::Progress;

using Collection = sdbusplus::common::com::ibm::vpd::Collection;

/*
 * The properties are defined as a variant type, which allows for different
 * types of values to be stored in the same variable. The properties are used to
 * determine type of the property value from the config
 *
 */

 using BMCProperties =
 std::variant<std::string, SyncBMCData::FullSyncStatus, Redundancy::Role,
              BootProgress::ProgressStages, Host::HostState,
              Collection::Status>;
using PropMap = std::unordered_map<std::string, BMCProperties>;
using InterfaceMap = std::map<std::string, PropMap>;

/*
* Map of the property name and the function to convert the property value to
* string. The function is used to convert the property value to string for
* comparison with the expected values from the config.
*
* this is used in the watchBmcPropertiesChanged function to convert the
* property value to string for comparison with the expected values from the
* config.
*
*/

const std::unordered_map<
 std::string, std::function<std::string(const PropMap::mapped_type&)>>
 conversionMap = {{"CurrentHostState",
                   [](const PropMap::mapped_type& var) -> std::string {
 if (std::holds_alternative<Host::HostState>(var))
 {
     return Host::convertHostStateToString(std::get<Host::HostState>(var));
 }
 return "";
}},
                  {"BootProgress",
                   [](const PropMap::mapped_type& var) -> std::string {
 if (std::holds_alternative<BootProgress::ProgressStages>(var))
 {
     return BootProgress::convertProgressStagesToString(
         std::get<BootProgress::ProgressStages>(var));
 }
 return "";
}},
                  {"CollectionStatus",
                   [](const PropMap::mapped_type& var) -> std::string {
 if (std::holds_alternative<Collection::Status>(var))
 {
     return Collection::convertStatusToString(
         std::get<Collection::Status>(var));
 }
 return "";
}}};

StateDrivenSync::StateDrivenSync(sdbusplus::async::context& ctx,
                                 data_sync::Manager& manager) :
    _ctx(ctx), _manager(manager)
{}

sdbusplus::async::task<std::vector<std::string>>
    StateDrivenSync::getInterfaceObjectPaths(const std::string& interface)
{
    std::vector<std::string> str;
    str.push_back(interface);
    using ObjectMapper =
        sdbusplus::client::xyz::openbmc_project::ObjectMapper<>;

    [[maybe_unused]] auto mapper = ObjectMapper(_ctx)
                                       .service(ObjectMapper::default_service)
                                       .path(ObjectMapper::instance_path);

    std::vector<std::string> paths = co_await mapper.get_sub_tree_paths("/", 0,
                                                                        str);

    std::vector<std::string> result;
    for (const auto& path : paths)
    {
        result.push_back(path);
    }

    co_return result;
}

sdbusplus::async::task<std::string>
    StateDrivenSync::getServiceID(const std::string& objectPath,
                                  const std::string& interface)
{
    using ObjectMapper =
        sdbusplus::client::xyz::openbmc_project::ObjectMapper<>;

    try
    {
        [[maybe_unused]] auto mapper =
            ObjectMapper(_ctx)
                .service(ObjectMapper::default_service)
                .path(ObjectMapper::instance_path);

        std::vector<std::string> str;
        str.push_back(interface);
        auto object = co_await mapper.get_object(objectPath, str);
        co_return object.begin()->first;
    }
    catch (const sdbusplus::exception_t&)
    {}
    co_return std::string{};
}

sdbusplus::async::task<>
    StateDrivenSync::watchBmcPropertiesChanged(const std::string dbusInterface)
{
    lg2::debug("Inside watchBmcPropertiesChanged");

    auto itGroup = _manager.watcherLists.find(dbusInterface);
    if (itGroup == _manager.watcherLists.end())
    {
        co_return;
    }

    auto& controlledConfigs = itGroup->second;
    std::string interface = dbusInterface;

    auto objectPaths = co_await getInterfaceObjectPaths(interface);

    while (objectPaths.empty())
    {
        std::cout << "No object paths found for interface: "
                  << interface << std::endl;
        objectPaths = co_await getInterfaceObjectPaths(interface);

        co_await sdbusplus::async::sleep_for(_ctx,
                                             std::chrono::milliseconds(50));
    }

    std::string objectPath = objectPaths.front();

    auto ServiceID = co_await getServiceID(objectPath, interface);

    auto watcher = sdbusplus::async::proxy()
                       .service(ServiceID)
                       .path(objectPath)
                       .interface(interface);

    auto props = co_await watcher.get_all_properties<BMCProperties>(_ctx);

    auto processCriteria =
        [&](const auto& criteriaMap,
            auto&& setFlagAsync) -> sdbusplus::async::task<> {
        for (const auto& [propName, expectedVals] : criteriaMap)
        {
            auto itProp = props.find(propName);
            if (itProp == props.end())
            {
                continue;
            }
            std::string value;
            auto itConv = conversionMap.find(propName);
            if (itConv != conversionMap.end())
            {
                value = itConv->second(itProp->second);
            }

            std::cout << "Property: " << propName << ", Value: " << value
                      << std::endl;

            if (!value.empty() &&
                expectedVals.find(value) != expectedVals.end())
            {
                co_await setFlagAsync();
            }
        }
    };

    for (auto* cfg : controlledConfigs)
    {
        auto& stateSync = cfg->_stateDrivenSync.value();
        auto itState = stateSync._interfaces.find(interface);
        if (itState == stateSync._interfaces.end())
        {
            continue;
        }
        auto& stateInfo = itState->second;

        co_await processCriteria(stateInfo._suspendStates,
                                 [&]() -> sdbusplus::async::task<> {
            stateSync._suspendSync = true;
            std::cout << "At the starting the Suspend flag is : " << stateSync._suspendSync
                    <<" because property value is matched with Json config value" << std::endl;

            co_return;
        });

        co_await processCriteria(
            stateInfo._resumeStates,
            [this, cfg, &stateSync, objectPath]() -> sdbusplus::async::task<> {
            stateSync._suspendSync = false;
            std::cout << "At the starting the Suspend flag is : " << stateSync._suspendSync
                      <<" because property value is not matched with Json config value" << std::endl;
            // FIXME:
            bool result = co_await _manager.syncCallback(*cfg);

            if (result)
            {
                lg2::info("Sync succeeded for {PATH}", "PATH", objectPath);
            }
            else
            {
                lg2::error("Sync failed for {PATH}", "PATH", objectPath);
            }
            co_return;
        });
    }

    sdbusplus::async::match match(
        _ctx, rules::propertiesChanged(objectPath, interface));

    while (!_ctx.stop_requested())
    {
        auto [_, properties] = co_await match.next<std::string, PropMap>();

        auto processCriteria =
            [&](const auto& criteriaMap,
                auto&& setFlagAsync) -> sdbusplus::async::task<> {
            for (const auto& [propName, expectedVals] : criteriaMap)
            {
                auto itProp = properties.find(propName);
                if (itProp == properties.end())
                {
                    continue;
                }
                std::string value;
                auto itConv = conversionMap.find(propName);
                if (itConv != conversionMap.end())
                {
                    value = itConv->second(itProp->second);
                }

                std::cout << "Property: " << propName << ", Value: " << value
                          << std::endl;

                if (!value.empty() &&
                    expectedVals.find(value) != expectedVals.end())
                {
                    co_await setFlagAsync();
                }
            }
        };

        for (auto* cfg : controlledConfigs)
        {
            auto& stateSync = cfg->_stateDrivenSync.value();
            auto itState = stateSync._interfaces.find(interface);
            if (itState == stateSync._interfaces.end())
            {
                continue;
            }
            auto& stateInfo = itState->second;

            co_await processCriteria(stateInfo._suspendStates,
                                     [&]() -> sdbusplus::async::task<> {
                stateSync._suspendSync = true;
                co_return;
            });

            co_await processCriteria(stateInfo._resumeStates,
                                     [this, cfg, &stateSync, objectPath]()
                                         -> sdbusplus::async::task<> {
                stateSync._suspendSync = false;

                bool result = co_await _manager.syncCallback(*cfg);

                if (result)
                {
                    lg2::info("Sync succeeded for {PATH}", "PATH", objectPath);
                }
                else
                {
                    lg2::error("Sync failed for {PATH}", "PATH", objectPath);
                }
                co_return;
            });
        }
    }
    co_return;
}

} // namespace data_sync::state_driven
