// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <sdbusplus/async.hpp>

namespace datasynctool::test_sync
{

/**
 * @brief Test the rsync/stunnel path with a dry-run sync.
 *
 * Uses rsync's dry-run mode to verify the sync path without modifying the
 * sibling BMC. On failure, recent stunnel journal entries are used for
 * diagnosis and included in the output when requested.
 *
 * @param[in] ctx - Async context used to query service status
 * @param[in] jsonOutput - Output in JSON format if true
 * @param[in] includeTrace - Include recent stunnel journal entries if true
 *
 */
sdbusplus::async::task<> run(sdbusplus::async::context& ctx, bool jsonOutput,
                             bool includeTrace);

} // namespace datasynctool::test_sync
