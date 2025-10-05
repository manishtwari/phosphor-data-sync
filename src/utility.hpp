// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "data_sync_config.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace data_sync::utility
{

/**
 * @class FD
 * @brief RAII wrapper for file descriptor.
 */
class FD
{
  public:
    FD(const FD&) = delete;
    FD& operator=(const FD&) = delete;
    FD(FD&&) = delete;
    FD& operator=(FD&&) = delete;

    /**
     * @brief Constructor
     *
     * Saves the file descriptor and uses it to do file operation
     *
     *  @param[in] fd - File descriptor
     */
    explicit FD(int fd);

    /**
     * @brief Destructor
     *
     * To close the file descriptor once goes out of scope.
     */
    ~FD();

    /**
     * @brief To close the file descriptor manually.
     */
    void reset();

    /**
     * @brief To return the saved file descriptor
     */
    int operator()() const;

  private:
    /**
     * @brief File descriptor
     */
    int fd = -1;
};

} // namespace data_sync::utility

namespace data_sync::retry
{
namespace fs = std::filesystem;
/**
 * @brief Extract the vanished path parsed from rsync's error output
 *
 * @param[in] rsyncCmdOut - rsync output text (stderr)
 * @return std::string the vanished source path
 *
 */
std::vector<std::filesystem::path>
    getVanishedSrcPaths(const std::string& rsyncCmdOut);

/**
 * @brief Frame an rsync command that retries only IncludeList entries
 *        located under the given vanished source paths
 *
 *        builds a CLI with '--include' filters for matching IncludeList entries
 *        and a final '--exclude=*' so only the required paths are retried
 *
 * @param[in] cfg - The data sync config
 * @param[in] vanishedRoots - List of source root paths reported as vanished
 * @return std::string The framed rsync command line
 *
 */
std::string frameIncludeListCLI(
    const config::DataSyncConfig& cfg,
    const std::vector<std::filesystem::path>& vanishedRoots);

} // namespace data_sync::retry
