// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "data_sync_config.hpp"

#include <phosphor-logging/lg2.hpp>
#include <sdbusplus/async.hpp>
#include <xyz/openbmc_project/ObjectMapper/client.hpp>

namespace data_sync
{

namespace rules = sdbusplus::bus::match::rules;
using namespace std::string_literals;

using SubTreeType =
    std::map<std::string, std::map<std::string, std::vector<std::string>>>;

using BMCProperties =
    std::variant<std::string, uint64_t, std::set<std::string>>;

using PropertiesMap = std::unordered_map<std::string, BMCProperties>;

class Manager;

namespace state_driven
{

/**
 * @class StateDrivenSync
 *
 * @brief This class manages the state-driven synchronization of data
 *        between BMCs.
 */
class StateDrivenSync
{
  public:
    StateDrivenSync(const StateDrivenSync&) = delete;
    StateDrivenSync& operator=(const StateDrivenSync&) = delete;
    StateDrivenSync(StateDrivenSync&&) = delete;
    StateDrivenSync& operator=(StateDrivenSync&&) = delete;
    virtual ~StateDrivenSync() = default;

    /**
     * @brief Constructor for StateDrivenSync
     *
     * @param[in] ctx Reference to the async D-Bus context
     * @param[in] manager Reference of the manager
     */
    StateDrivenSync(sdbusplus::async::context& ctx,
                    data_sync::Manager& manager);

    /**
     * @brief Retrieves Instance paths for a given D-Bus interface
     *
     *        - Queries the object mapper for all object paths for
     *          the specified interface
     *
     * @param[in] interface D-Bus interface name
     *
     * @return A map of object paths to their corresponding services and
     *         interfaces
     */
    sdbusplus::async::task<SubTreeType>
        getSubTree(const std::string& interface);

    /**
     * @brief Monitors property changes on a specified D-Bus interface
     *
     *       - Listens for property change signals on the object path associated
     *         with the interface
     *
     * @param[in] key data config object to monitor for property changes
     *
     */
    sdbusplus::async::task<> watchBmcPropertiesChanged(std::string interface);

    /**
     * @brief Constructs a D-Bus match rule for PropertiesChanged signal
     *
     *        - Filters for signals from the specified service, interface, and
     *          object path
     *        - Specifically targets the PropertiesChanged signal on the given
     *          interface
     *
     * @param[in] service The D-Bus service
     * @param[in] interface The D-Bus interface
     * @param[in] objectPath The D-Bus object path
     *
     * @return A match rule string for subscribing to the signal
     */
    static auto propChangeRule(const std::string& service,
                               const std::string& interface,
                               const std::string& objectPath) noexcept
    {
        return rules::type::signal()
            .append(rules::sender(service))
            .append(rules::member("PropertiesChanged"s))
            .append(rules::interface("org.freedesktop.DBus.Properties"s))
            .append(rules::argN(0, interface))
            .append(rules::path(objectPath));
    }

    /**
     * @brief Fetches all properties for a given D-Bus interface
     *
     * @param[in] service The D-Bus service name providing the interface
     * @param[in] objectPath The object path exposing the interface
     * @param[in] interface The interface whose properties are to be
     *                      retrieved
     *
     * @return A map of property names to their values
     */
    sdbusplus::async::task<PropertiesMap>
        getAllProperties(const std::string& service,
                         const std::string& objectPath,
                         const std::string& interface);

    /**
     * @brief Updates sync state based on the current property values
     *
     *        - Evaluates properties against suspend/resume criteria
     *        - If suspend criteria are met, disables sync
     *        - If resume criteria are met, re-enables sync and triggers sync
     *          callback
     *
     * @param[in] props Map of current property values
     * @param[in] InterfaceName The D-Bus interface associated with these
     *                          properties
     */
    sdbusplus::async::task<>
        updateSyncStateBasedOnProps(const PropertiesMap& props,
                                    const std::string& InterfaceName);

    /**
     * @brief Extracts the object path for a given interface and service from
     *        the subtree
     *
     * @param[in] subtree Subtree containing object paths and associated
     *                    services/interfaces
     * @param[in] service D-Bus service name to look for
     * @param[in] interface D-Bus interface name to match
     *
     * @return The matching object path if found, otherwise an empty string
     *
     */
    sdbusplus::async::task<std::string>
        extractObjPathFromSubtree(const auto& subtree,
                                  const std::string& service,
                                  const std::string& interface);

    /**
     * @brief Waits until the specified D-Bus interface becomes available for a
     *        given service
     *
     * @param[in] interfaceName The D-Bus interface to monitor
     * @param[in] serviceName The D-Bus service that provides the interface
     *
     * @return The object path where the interface is available, or empty if
     *         unavailable
     *
     */
    sdbusplus::async::task<std::string>
        waitUntilIFaceAvailable(const std::string& interfaceName,
                                const std::string& serviceName);

    /**
     * @brief Continuously monitors a D-Bus service
     *
     * @param[in] service The D-Bus service to monitor
     * @param[in] interface The D-Bus interface associated with the service
     *
     */
    sdbusplus::async::task<> monitorServiceAvailability(std::string service,
                                                        std::string interface);

    /**
     * @brief Resets the suspend flag for the specified interface
     *
     * @param[in] interface The D-Bus interface
     *
     */
    sdbusplus::async::task<> updateSuspendFlag(const std::string& interface);

    /**
     * @brief Extracts the service name from config
     *
     * @param[in] dataSyncCfgs List of configuration pointers
     * @return The extracted D-Bus service name
     *
     */

    sdbusplus::async::task<std::string>
        getServiceFromCfg(const auto& dataSyncCfgs);

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
