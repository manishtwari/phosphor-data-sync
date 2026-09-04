// SPDX-License-Identifier: Apache-2.0

#include "config.h"

#include "test_sync.hpp"

#include "dbus_interactions.hpp"
#include "err_reason_rules.hpp"
#include "error_summary.hpp"
#include "utility.hpp"
#include "utils.hpp"

#include <nlohmann/json.hpp>

#include <exception>
#include <format>
#include <fstream>
#include <print>
#include <string>
#include <string_view>
#include <vector>

namespace datasynctool::test_sync
{

namespace
{

using json = nlohmann::ordered_json;

// Fixed DataSync-owned file used as the source of the rsync dry run.
constexpr auto testFilePath = "/var/lib/phosphor-data-sync/datasync_test";

// Capture the newest stunnel entries for failure diagnosis and -T output.
constexpr std::string_view stunnelJournalCommand =
    "journalctl --quiet --no-pager --reverse --lines=10 "
    "--unit=SyncBMCData_stunnel.service 2>&1";

void printFailure(std::string_view message, bool jsonOutput)
{
    if (jsonOutput)
    {
        json result;
        result["ErrorReason"] = message;
        std::println("{}", result.dump(4));
        return;
    }

    std::println("  {:<18}: {}", "ErrorReason", message);
}

} // namespace

sdbusplus::async::task<> run(sdbusplus::async::context& ctx, bool jsonOutput,
                             bool includeTrace)
{
    const auto rsyncServiceStatus =
        // NOLINTNEXTLINE
        co_await dbus_interactions::getServiceActiveState(
            ctx, "SyncBMCData_rsync.service");

    const auto stunnelServiceStatus =
        // NOLINTNEXTLINE
        co_await dbus_interactions::getServiceActiveState(
            ctx, "SyncBMCData_stunnel.service");

    if (rsyncServiceStatus != "active" || stunnelServiceStatus != "active")
    {
        printFailure(std::format("DataSync services are not active (rsync: {}, "
                                 "stunnel: {})",
                                 rsyncServiceStatus, stunnelServiceStatus),
                     jsonOutput);
        co_return;
    }

    std::size_t bmcPosition;
    try
    {
        bmcPosition = data_sync::utility::readBMCPosition();
    }
    catch (const std::exception& error)
    {
        printFailure(error.what(), jsonOutput);
        co_return;
    }

    // The fixed probe remains only on the local BMC. Rsync's --dry-run option
    // prevents it from being created on the sibling BMC.
    std::ofstream testFile(testFilePath);
    if (!testFile.is_open())
    {
        printFailure("Unable to create the local test file", jsonOutput);
        co_return;
    }
    testFile.close();

    const std::string rsyncTarget =
        std::format("rsync://localhost:{}/{}",
                    bmcPosition == 0 ? BMC1_RSYNC_PORT : BMC0_RSYNC_PORT,
                    RSYNCD_MODULE_NAME);

    // Keep the full path so the daemon accepts the file.
    const std::string command = std::format(
        "rsync --dry-run --relative {} {} 2>&1", testFilePath, rsyncTarget);

    const auto [exitCode, rsyncOutput] = utils::runCommand(command);
    if (exitCode == 0)
    {
        if (jsonOutput)
        {
            json result;
            result["Dry Sync"] = "Passed";
            std::println("{}", result.dump(4));
        }
        else
        {
            std::println("Dry sync passed successfully.");
        }
        co_return;
    }

    // The rsync service does not emit useful journal entries. Use the most
    // recent stunnel entries together with rsync's command output to diagnose
    // the failure.
    const auto stunnelTrace = utils::runCommand(stunnelJournalCommand).second;

    // deriveErrorReason() scans in reverse, so check the current rsync output
    // before the recent journal history.
    const std::vector<std::string> diagnosticOutput{stunnelTrace, rsyncOutput};

    error_summary::SummaryEntry entry;
    entry.rsyncErrMsg = rsyncOutput;
    entry.rsyncErrCode = std::to_string(exitCode);

    if (const auto reason = error_summary::deriveErrorReason(diagnosticOutput);
        reason.has_value())
    {
        entry.errReason = reason->reason;
        for (const auto cause : reason->causes)
        {
            entry.errCauses.emplace_back(cause);
        }
        for (const auto verify : reason->verify)
        {
            entry.errVerify.emplace_back(verify);
        }
    }

    if (jsonOutput)
    {
        json result = error_summary::syncFailureToJson(entry);
        if (includeTrace)
        {
            result["Trace"] = stunnelTrace;
        }
        std::println("{}", result.dump(4));
    }
    else
    {
        error_summary::printSyncFailureEntry(entry);
        if (includeTrace && !stunnelTrace.empty())
        {
            std::println("  {:<18}:", "Trace");
            std::print("{}", stunnelTrace);
        }
    }

    co_return;
}

} // namespace datasynctool::test_sync
