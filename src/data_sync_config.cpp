// SPDX-License-Identifier: Apache-2.0

#include "config.h"

#include "data_sync_config.hpp"

#include <phosphor-logging/lg2.hpp>

#include <iostream>
#include <regex>

namespace data_sync::config
{

Retry::Retry(uint8_t retryAttempts,
             const std::chrono::seconds& retryIntervalInSec) :
    _retryAttempts(retryAttempts), _retryIntervalInSec(retryIntervalInSec)
{}

bool Retry::operator==(const Retry& retry) const
{
    return _retryAttempts == retry._retryAttempts &&
           _retryIntervalInSec == retry._retryIntervalInSec;
}

DataSyncConfig::DataSyncConfig(const nlohmann::json& config,
                               const bool isPathDir) :
    _path(config["Path"].get<std::string>()), _isPathDir(isPathDir),
    _syncDirection(
        convertSyncDirectionToEnum(config["SyncDirection"].get<std::string>())
            .value_or(SyncDirection::Active2Passive)),
    _syncType(convertSyncTypeToEnum(config["SyncType"].get<std::string>())
                  .value_or(SyncType::Immediate)),
    _stateDrivenSync(StateDrivenSync())
{
    // Initiailze optional members
    if (config.contains("DestinationPath"))
    {
        _destPath = config["DestinationPath"].get<std::string>();
    }
    else
    {
        _destPath = std::nullopt;
    }

    if (_syncType == SyncType::Periodic)
    {
        constexpr auto defPeriodicity = 60;
        _periodicityInSec =
            convertISODurationToSec(config["Periodicity"].get<std::string>())
                .value_or(std::chrono::seconds(defPeriodicity));
    }
    else
    {
        _periodicityInSec = std::nullopt;
    }

    if (config.contains("RetryAttempts") && config.contains("RetryInterval"))
    {
        _retry = Retry(
            config["RetryAttempts"].get<std::uint8_t>(),
            convertISODurationToSec(config["RetryInterval"].get<std::string>())
                .value_or(std::chrono::seconds(DEFAULT_RETRY_INTERVAL)));
    }
    else
    {
        _retry = std::nullopt;
    }

    if (config.contains("ExcludeFilesList"))
    {
        _excludeFileList =
            config["ExcludeFilesList"].get<std::vector<std::string>>();
    }
    else
    {
        _excludeFileList = std::nullopt;
    }

    if (config.contains("IncludeFilesList"))
    {
        _includeFileList =
            config["IncludeFilesList"].get<std::vector<std::string>>();
    }
    else
    {
        _includeFileList = std::nullopt;
    }
    if (config.contains("StateDrivenSync"))
    {
        const auto& syncArray = config["StateDrivenSync"];

        for (const auto& syncObj : syncArray)
        {
            for (const auto& [interfaceName, interfaceObj] : syncObj.items())
            {
                StateInfo stateInfo;

                if (interfaceObj.contains("SuspendStates"))
                {
                    const auto& suspendStates = interfaceObj["SuspendStates"];
                    for (const auto& [stateType, stateList] :
                         suspendStates.items())
                    {
                        for (const auto& state : stateList)
                        {
                            stateInfo._suspendStates[stateType].insert(state);
                        }
                    }
                }

                if (interfaceObj.contains("ResumeStates"))
                {
                    const auto& resumeStates = interfaceObj["ResumeStates"];
                    for (const auto& [stateType, stateList] :
                         resumeStates.items())
                    {
                        for (const auto& state : stateList)
                        {
                            stateInfo._resumeStates[stateType].insert(state);
                        }
                    }
                }

                _stateDrivenSync->_interfaces.emplace(interfaceName,
                                                      std::move(stateInfo));
            }
        }
        // printStateDrivenSync();
    }
    else
    {
        _stateDrivenSync = std::nullopt;
    }
}

void DataSyncConfig::printStateDrivenSync()
{
    if (!_stateDrivenSync.has_value())
    {
        std::cout << "__stateDrivenSync is not set.\n";
        return;
    }

    const auto& sync = _stateDrivenSync.value();
    std::cout << "SuspendSync Flag: " << std::boolalpha << sync._suspendSync
              << "\n";

    for (const auto& [interface, stateInfo] : sync._interfaces)
    {
        std::cout << "Interface: " << interface << "\n";

        std::cout << "  Services: " << stateInfo._serviceName << "\n";

        std::cout << "  SuspendStates:\n";
        for (const auto& [prop, values] : stateInfo._suspendStates)
        {
            std::cout << "    " << prop << ": ";
            for (const auto& val : values)
            {
                std::cout << val << " ";
            }
            std::cout << "\n";
        }

        std::cout << "  ResumeStates:\n";
        for (const auto& [prop, values] : stateInfo._resumeStates)
        {
            std::cout << "    " << prop << ": ";
            for (const auto& val : values)
            {
                std::cout << val << " ";
            }
            std::cout << "\n\n\n";
        }
    }
}

bool DataSyncConfig::operator==(const DataSyncConfig& dataSyncCfg) const
{
    return _path == dataSyncCfg._path &&
           _syncDirection == dataSyncCfg._syncDirection &&
           _destPath == dataSyncCfg._destPath &&
           _syncType == dataSyncCfg._syncType &&
           _periodicityInSec == dataSyncCfg._periodicityInSec &&
           _retry == dataSyncCfg._retry &&
           _excludeFileList == dataSyncCfg._excludeFileList &&
           _includeFileList == dataSyncCfg._includeFileList;
}

std::optional<SyncDirection>
    DataSyncConfig::convertSyncDirectionToEnum(const std::string& syncDirection)
{
    if (syncDirection == "Active2Passive")
    {
        return SyncDirection::Active2Passive;
    }
    else if (syncDirection == "Passive2Active")
    {
        return SyncDirection::Passive2Active;
    }
    else if (syncDirection == "Bidirectional")
    {
        return SyncDirection::Bidirectional;
    }
    else
    {
        lg2::error("Unsupported sync direction [{SYNC_DIRECTION}]",
                   "SYNC_DIRECTION", syncDirection);
        return std::nullopt;
    }
}

std::optional<SyncType>
    DataSyncConfig::convertSyncTypeToEnum(const std::string& syncType)
{
    if (syncType == "Immediate")
    {
        return SyncType::Immediate;
    }
    else if (syncType == "Periodic")
    {
        return SyncType::Periodic;
    }
    else
    {
        lg2::error("Unsupported sync type [{SYNC_TYPE}]", "SYNC_TYPE",
                   syncType);
        return std::nullopt;
    }
}

std::optional<std::chrono::seconds> DataSyncConfig::convertISODurationToSec(
    const std::string& timeIntervalInISO)
{
    // TODO: Parse periodicity in ISO 8601 duration format using
    // std::chrono::parse, as the API is not available in the current gcc
    // compiler (13.3.0).
    std::smatch match;
    std::regex isoDurationRegex("PT(([0-9]+)H)?(([0-9]+)M)?(([0-9]+)S)?");

    if (std::regex_search(timeIntervalInISO, match, isoDurationRegex))
    {
        return (std::chrono::seconds(
            (match.str(2).empty() ? 0 : (std::stoi(match.str(2)) * 60 * 60)) +
            (match.str(4).empty() ? 0 : (std::stoi(match.str(4)) * 60)) +
            (match.str(6).empty() ? 0 : std::stoi(match.str(6)))));
    }
    else
    {
        lg2::error("{TIME_INTERVAL} is not matching with expected "
                   "ISO 8601 duration format [PTnHnMnS]",
                   "TIME_INTERVAL", timeIntervalInISO);
        return std::nullopt;
    }
}

} // namespace data_sync::config
