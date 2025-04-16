// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "data_sync_config.hpp"

#include <phosphor-logging/lg2.hpp>
#include <sdbusplus/async.hpp>
#include <xyz/openbmc_project/Control/SyncBMCData/aserver.hpp>
#include <xyz/openbmc_project/ObjectMapper/client.hpp>
#include <xyz/openbmc_project/ObjectMapper/common.hpp>
#include <xyz/openbmc_project/State/BMC/Redundancy/common.hpp>

#include <xyz/openbmc_project/State/Host/common.hpp>
#include <xyz/openbmc_project/State/Boot/Progress/common.hpp>
#include <xyz/openbmc_project/State/Boot/PostCode/common.hpp>
#include <xyz/openbmc_project/State/Boot/Raw/common.hpp>
#include <com/ibm/VPD/Collection/common.hpp>

namespace rules = sdbusplus::bus::match::rules;

namespace data_sync
{
class Manager;

namespace state_driven
{
class StateDrivenSync
{
  public:
    StateDrivenSync(const StateDrivenSync&) = delete;
    StateDrivenSync& operator=(const StateDrivenSync&) = delete;
    StateDrivenSync(StateDrivenSync&&) = delete;
    StateDrivenSync& operator=(StateDrivenSync&&) = delete;
    virtual ~StateDrivenSync() = default;

    /**
     * @brief Constructor for StateDrivenSync.
     *
     * @param[in] ctx Reference to the async D-Bus context.
     * @param[in] manager Reference of the manager.
     */
    StateDrivenSync(sdbusplus::async::context& ctx,
                    data_sync::Manager& manager);

    /**
     * @brief Retrieves Instance paths for a given D-Bus interface
     *
     *        - Queries the object mapper for all object paths for
     *          the specified interface
     *
     * @param[in] interface The D-Bus interface name
     *
     * @return returns a vector of object's paths
     *
     */
    sdbusplus::async::task<std::vector<std::string>>
        getInterfaceObjectPaths(const std::string &interface);

    sdbusplus::async::task<std::string> getServiceID(const std::string &objectPath,
                                          const std::string &interface);

    /**
     * @brief Monitors property changes on a specified D-Bus interface
     *
     *       - Listens for property change signals on the object path associated
     *         with the interface
     *       - Processes suspend and resume state based on the current
     *         configuration
     *
     * @param[in] key The D-Bus interface name to monitor for property changes
     *
     */
    sdbusplus::async::task<>
        watchBmcPropertiesChanged(const std::string interface);

  private:
    /**
     * @brief The async context object used to perform operations
     *        asynchronously as required
     */
    sdbusplus::async::context& _ctx;

    /**
     * @brief Reference to the Manager object
     */
    Manager& _manager;
};

} // namespace state_driven
} // namespace data_sync
