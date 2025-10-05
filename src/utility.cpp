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
std::vector<fs::path> getVanishedSrcPaths(const std::string& rsyncCmdOut)
{
    const std::string vanishPattern = "file has vanished: \"";
    std::vector<fs::path> vanishSrcs;
    size_t searchPos = 0;

    while ((searchPos = rsyncCmdOut.find(vanishPattern, searchPos)) !=
           std::string::npos)
    {
        searchPos += vanishPattern.size();
        std::size_t endQuote = rsyncCmdOut.find('"', searchPos);
        if (endQuote == std::string::npos)
        {
            break;
        }
        vanishSrcs.emplace_back(
            rsyncCmdOut.substr(searchPos, endQuote - searchPos));
        searchPos = endQuote + 1;
    }
    return vanishSrcs;
}

std::string
    frameIncludeListCLI(const config::DataSyncConfig& cfg,
                        const std::vector<std::filesystem::path>& vanishedRoots)
{
    auto isParentOfChild = [](const fs::path& child, const fs::path& parent) {
        fs::path rel = child.lexically_relative(parent);
        return !rel.empty() && !rel.string().starts_with("..");
    };

    auto addSlash = [](std::string str) {
        while (!str.empty() && str.back() == '/')
        {
            str.pop_back();
        }
        str.push_back('/');
        return str;
    };

    auto hasTrailingSlash = [](const fs::path& path) {
        std::string pathStr = path.string();
        return !pathStr.empty() && pathStr.back() == '/';
    };

    std::map<fs::path, std::vector<fs::path>> includesByRoot;
    for (const auto& vanishedPath : vanishedRoots)
    {
        fs::path normalizedVanishedPath = vanishedPath.lexically_normal();
        for (const auto& includedPath : *cfg._includeList)
        {
            if (isParentOfChild(includedPath, normalizedVanishedPath))
            {
                includesByRoot[normalizedVanishedPath].push_back(includedPath);
            }
        }
    }

    std::vector<std::string> includeArgs;
    std::unordered_set<std::string> uniqueIncludes;
    std::vector<std::string> sourcePaths;
    std::unordered_set<std::string> uniqueSources;

    auto addIncludeArgIfUnique = [&](std::string str) {
        if (uniqueIncludes.insert(str).second)
        {
            includeArgs.emplace_back(std::move(str));
        }
    };

    for (const auto& [rootPath, includedPaths] : includesByRoot)
    {
        const fs::path root = rootPath.lexically_normal();

        fs::path pathAccum;
        for (const auto& it : root)
        {
            if (it == "/")
            {
                continue;
            }
            pathAccum /= it;
            addIncludeArgIfUnique(" --include=" +
                                  addSlash(pathAccum.generic_string()));
        }

        for (const auto& originalIncludePath : includedPaths)
        {
            const bool isDir = hasTrailingSlash(originalIncludePath);
            fs::path normalizedIncludePath =
                originalIncludePath.lexically_normal();

            fs::path rel = normalizedIncludePath.lexically_relative(root);
            if (rel.empty() || rel.string().starts_with(".."))
            {
                continue;
            }

            fs::path relAccum;
            for (auto it = rel.begin(); it != rel.end(); ++it)
            {
                auto next = std::next(it);
                relAccum /= *it;
                if (next != rel.end())
                {
                    addIncludeArgIfUnique(" --include=" +
                                          addSlash(relAccum.generic_string()));
                }
            }

            const std::string leaf = rel.generic_string();
            if (isDir)
            {
                addIncludeArgIfUnique(" --include=" + addSlash(leaf) + "***");
            }
            else
            {
                addIncludeArgIfUnique(" --include=" + leaf);
            }
        }

        std::string src = " " + addSlash(root.string());
        if (uniqueSources.insert(src).second)
        {
            sourcePaths.emplace_back(std::move(src));
        }
    }

    std::string resultString;
    for (const auto& t : includeArgs)
    {
        resultString += t;
    }

    resultString += " --exclude=*";

    for (const auto& str : sourcePaths)
    {
        resultString += str;
    }
    return resultString;
}

} // namespace data_sync::retry
