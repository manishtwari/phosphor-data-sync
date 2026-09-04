// SPDX-License-Identifier: Apache-2.0

#include "utils.hpp"

#include <sys/wait.h>

#include <array>
#include <cerrno>
#include <cstdio>
#include <print>
#include <span>
#include <system_error>
#include <utility>

namespace datasynctool::utils
{

std::string extractEnumValue(const std::string& dbusValue)
{
    auto lastDot = dbusValue.find_last_of('.');
    if (lastDot != std::string::npos)
    {
        return dbusValue.substr(lastDot + 1);
    }
    return dbusValue;
}

std::string normalizePath(const std::string& path)
{
    if (path.empty() || path == "/")
    {
        return path;
    }

    // Remove trailing slashes
    std::string normalized = path;
    while (normalized.length() > 1 && normalized.back() == '/')
    {
        normalized.pop_back();
    }

    return normalized;
}

template <typename T>
void printParam(std::string key, const T& value)
{
    key.push_back(':');
    std::println("{:25}{}", key, value);
}

// Explicit template instantiations for common types
template void printParam<bool>(std::string, const bool&);
template void printParam<int>(std::string, const int&);
template void printParam<std::string>(std::string, const std::string&);

void displayJsonAsText(const json& data)
{
    std::println();

    for (const auto& [name, value] : data.items())
    {
        if (value.is_array())
        {
            std::println("{}:", name);
            for (const auto& item : value)
            {
                if (item.is_object())
                {
                    for (const auto& [key, val] : item.items())
                    {
                        if (val.is_array())
                        {
                            std::println("    {}:", key);
                            for (const auto& arrItem : val)
                            {
                                std::println("        {}",
                                             arrItem.is_string()
                                                 ? arrItem.get<std::string>()
                                                 : arrItem.dump());
                            }
                        }
                        else if (val.is_string())
                        {
                            std::println("    {}: {}", key,
                                         val.get<std::string>());
                        }
                        else if (val.is_boolean())
                        {
                            std::println("    {}: {}", key, val.get<bool>());
                        }
                        else if (val.is_number_integer())
                        {
                            std::println("    {}: {}", key, val.get<int>());
                        }
                        else
                        {
                            std::println("    {}: {}", key, val.dump());
                        }
                    }
                    std::println();
                }
                // Handle array of strings
                else if (item.is_string())
                {
                    std::println("  {}", item.get<std::string>());
                }
                else
                {
                    std::println("  {}", item.dump());
                }
            }
        }
        else if (value.is_boolean())
        {
            printParam(name, value.get<bool>());
        }
        else if (value.is_string())
        {
            printParam(name, value.get<std::string>());
        }
        else if (value.is_number_integer())
        {
            printParam(name, value.get<int>());
        }
        else
        {
            printParam(name, value.dump());
        }
    }
    std::println();
}

std::pair<int, std::string> runCommand(std::string_view cmd)
{
    constexpr std::size_t readBufferSize = 4096;

    // NOLINTNEXTLINE
    FILE* pipe = popen(std::string(cmd).c_str(), "r");
    if (pipe == nullptr)
    {
        std::println(stderr, "popen failed for command: {}", cmd);
        return {-1, {}};
    }

    std::string output;
    output.reserve(readBufferSize);
    std::array<char, readBufferSize> buffer{};
    std::size_t bytesRead = 0;
    while ((bytesRead = std::fread(buffer.data(), 1, buffer.size(), pipe)) > 0)
    {
        const auto chunk = std::span(buffer).first(bytesRead);
        output.append(chunk.begin(), chunk.end());
    }

    const int status = pclose(pipe);
    if (status == -1)
    {
        std::println(stderr, "pclose failed: {}",
                     std::system_category().message(errno));
        return {-1, std::move(output)};
    }

    if (WIFEXITED(status))
    {
        return {WEXITSTATUS(status), std::move(output)};
    }

    return {-1, std::move(output)};
}

} // namespace datasynctool::utils
