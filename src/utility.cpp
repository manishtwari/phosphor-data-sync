// SPDX-License-Identifier: Apache-2.0

#include "utility.hpp"

#include <unistd.h>
namespace data_sync::utility
{

FD::FD(int fd) : fd(fd) {}

FD::~FD()
{
    reset();
}

void FD::reset()
{
    if (fd >= 0)
    {
        close(fd);
        fd = -1;
    }
}

int FD::operator()() const
{
    return fd;
}

} // namespace data_sync::utility

namespace data_sync::retry
{
std::string getVanishedSrcPath(const std::string& rsyncCmdOut)
{
    const std::string vanishPattern = "file has vanished: \"";
    std::string vanishSrcs;
    std::size_t searchPos = 0;

    while ((searchPos = rsyncCmdOut.find(vanishPattern, searchPos)) !=
           std::string::npos)
    {
        searchPos += vanishPattern.size();
        std::size_t endQuote = rsyncCmdOut.find('"', searchPos);
        if (endQuote == std::string::npos)
        {
            break;
        }
        if (!vanishSrcs.empty())
            vanishSrcs.push_back(' ');
        vanishSrcs.append(rsyncCmdOut, searchPos, endQuote - searchPos);

        searchPos = endQuote + 1;
    }
    return vanishSrcs;
}
} // namespace data_sync::retry
