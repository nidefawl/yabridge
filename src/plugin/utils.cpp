// yabridge: a Wine plugin bridge
// Copyright (C) 2020-2022 Robbert van der Helm
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include "utils.h"

#include <iostream>

#include <unistd.h>
#include <sstream>

// Generated inside of the build directory
#include <config.h>

#include "../common/configuration.h"
#include "../common/utils.h"

// FIXME: This should be passed as an argument instead
#include "../common/linking.h"

namespace fs = ghc::filesystem;

// These functions are used to populate the fields in `PluginInfo`. See the
// docstrings for the corresponding fields for more information on what we're
// actually doing here.
fs::path find_plugin_library(const fs::path& this_plugin_path,
                             PluginType plugin_type,
                             bool prefer_32bit_vst3);
fs::path normalize_plugin_path(const fs::path& windows_library_path,
                               PluginType plugin_type);
std::variant<OverridenWinePrefix, fs::path, DefaultWinePrefix> find_wine_prefix(
    fs::path windows_plugin_path);

PluginInfo::PluginInfo(PluginType plugin_type, bool prefer_32bit_vst3)
    : plugin_type_(plugin_type),
      native_library_path_(get_this_file_location()),
      // As explained in the docstring, this is the actual Windows library. For
      // VST3 plugins that come in a module we should be loading that module
      // instead of the `.vst3` file within in, which is where
      // `windows_plugin_path` comes in.
      windows_library_path_(find_plugin_library(native_library_path_,
                                                plugin_type,
                                                prefer_32bit_vst3)),
      plugin_arch_(find_dll_architecture(windows_library_path_)),
      windows_plugin_path_(
          normalize_plugin_path(windows_library_path_, plugin_type)),
      wine_prefix_(find_wine_prefix(windows_plugin_path_)) {}

ProcessEnvironment PluginInfo::create_host_env() const {
    ProcessEnvironment env(environ);

    // Only set the prefix when could auto detect it and it's not being
    // overridden (this entire `std::visit` instead of `std::has_alternative` is
    // just for clarity's sake)
    std::visit(overload{
                   [](const OverridenWinePrefix&) {},
                   [&](const ghc::filesystem::path& prefix) {
                       env.insert("WINEPREFIX", prefix.string());
                   },
                   [](const DefaultWinePrefix&) {},
               },
               wine_prefix_);

    return env;
}

ghc::filesystem::path PluginInfo::normalize_wine_prefix() const {
    return std::visit(
        overload{
            [](const OverridenWinePrefix& prefix) { return prefix.value; },
            [](const ghc::filesystem::path& prefix) { return prefix; },
            [](const DefaultWinePrefix&) {
                // NOLINTNEXTLINE(concurrency-mt-unsafe)
                const char* home_dir = getenv("HOME");
                assert(home_dir);

                return fs::path(home_dir) / ".wine";
            },
        },
        wine_prefix_);
}

std::string PluginInfo::wine_version() const {
    // The '*.exe' scripts generated by winegcc allow you to override the binary
    // used to run Wine, so will will handle this in the same way for our Wine
    // version detection. We'll be using `execvpe`
    std::string wine_path = "wine";
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    if (const char* wineloader_path = getenv("WINELOADER");
        wineloader_path && access(wineloader_path, X_OK) == 0) {
        wine_path = wineloader_path;
    }

    Process process(wine_path);
    process.arg("--version");
    process.environment(create_host_env());

    const auto result = process.spawn_get_stdout_line();
    return std::visit(
        overload{
            [](std::string version_string) -> std::string {
                // Strip the `wine-` prefix from the output, could potentially
                // be absent in custom Wine builds
                constexpr std::string_view version_prefix("wine-");
                if (version_string.starts_with(version_prefix)) {
                    version_string =
                        version_string.substr(version_prefix.size());
                }

                return version_string;
            },
            [](const Process::CommandNotFound&) -> std::string {
                return "<NOT FOUND>";
            },
            [](const std::error_code& err) -> std::string {
                return "<ERROR SPAWNING WINE: " + err.message() + " >";
            },
        },
        result);
}

fs::path find_plugin_library(const fs::path& this_plugin_path,
                             PluginType plugin_type,
                             bool prefer_32bit_vst3) {
    // TODO: We only consider lower case extensions, and yabridgectl also
    //       explicitly ignores upper and mixed case versions. Doing a case
    //       insensitive version of this would involve checking each entry in
    //       the directory listing. That's possible, but not something we're
    //       doing right now.
    switch (plugin_type) {
        case PluginType::vst2: {
            fs::path plugin_path(this_plugin_path);
            plugin_path.replace_extension(".dll");
            if (fs::exists(plugin_path)) {
                // Also resolve symlinks here, to support symlinked .dll files
                return fs::canonical(plugin_path);
            }

            // In case this files does not exist and our `.so` file is a
            // symlink, we'll also repeat this check after resolving that
            // symlink to support links to copies of `libyabridge-vst2.so` as
            // described in issue #3
            fs::path alternative_plugin_path = fs::canonical(this_plugin_path);
            alternative_plugin_path.replace_extension(".dll");
            if (fs::exists(alternative_plugin_path)) {
                return fs::canonical(alternative_plugin_path);
            }

            // This function is used in the constructor's initializer list so we
            // have to throw when the path could not be found
            throw std::runtime_error("'" + plugin_path.string() +
                                     "' does not exist, make sure to rename "
                                     "'libyabridge-vst2.so' to match a "
                                     "VST plugin .dll file.");
        } break;
        case PluginType::vst3: {
            // A VST3 plugin in Linux always has to be inside of a bundle (=
            // directory) named `X.vst3` that contains a shared object
            // `X.vst3/Contents/x86_64-linux/X.so`. On Linux `X.so` is not
            // allowed to be standalone, so for yabridge this should also always
            // be installed this way.
            // https://developer.steinberg.help/pages/viewpage.action?pageId=9798275
            const fs::path bundle_home =
                this_plugin_path.parent_path().parent_path().parent_path();
            const fs::path win_module_name =
                this_plugin_path.filename().replace_extension(".vst3");

            // Quick check in case the plugin was set up without yabridgectl,
            // since the format is very specific and any deviations from that
            // will be incorrect.
            if (bundle_home.extension() != ".vst3") {
                throw std::runtime_error(
                    "'" + this_plugin_path.string() +
                    "' is not inside of a VST3 bundle. Use yabridgectl to "
                    "set up yabridge for VST3 plugins or check the readme "
                    "for the correct format.");
            }

            // Finding the Windows plugin consists of two steps because
            // Steinberg changed the format around:
            // - First we'll find the plugin in the VST3 bundle created by
            //   yabridgectl in `~/.vst3/yabridge`. The plugin can be either
            //   32-bit or 64-bit. If both exist, then we'll take the 64-bit
            //   version, unless the `vst3_prefer_32bit` yabridge.toml option
            //   has been enabled for this plugin.
            // - After that we'll resolve the symlink to the module in the Wine
            //   prefix, and then we'll have to figure out if this module is an
            //   old style standalone module (< 3.6.10) or if it's inside of
            //   a bundle (>= 3.6.10)
            const fs::path candidate_path_64bit =
                bundle_home / "Contents" / "x86_64-win" / win_module_name;
            const fs::path candidate_path_32bit =
                bundle_home / "Contents" / "x86-win" / win_module_name;

            // After this we'll have to use `normalize_plugin_path()` to get the
            // actual module entry point in case the plugin is using a VST
            // 3.6.10 style bundle, because we need to inspect that for the
            // _actual_ (with yabridgectl `x86_64-win` should only contain a
            // 64-bit plugin and `x86-win` should only contain a 32-bit plugin,
            // but you never know!)
            // NOLINTNEXTLINE(bugprone-branch-clone)
            if (prefer_32bit_vst3 && fs::exists(candidate_path_32bit)) {
                return fs::canonical(candidate_path_32bit);
            } else if (fs::exists(candidate_path_64bit)) {
                return fs::canonical(candidate_path_64bit);
            } else if (fs::exists(candidate_path_32bit)) {
                return fs::canonical(candidate_path_32bit);
            }

            throw std::runtime_error(
                "'" + bundle_home.string() +
                "' does not contain a Windows VST3 module. Use yabridgectl to "
                "set up yabridge for VST3 plugins or check the readme "
                "for the correct format.");
        } break;
        default:
            throw std::runtime_error("How did you manage to get this?");
            break;
    }
}

fs::path normalize_plugin_path(const fs::path& windows_library_path,
                               PluginType plugin_type) {
    switch (plugin_type) {
        case PluginType::vst2:
            return windows_library_path;
            break;
        case PluginType::vst3: {
            // Now we'll have to figure out if this is a new-style bundle or
            // an old standalone module
            const fs::path win_module_name =
                windows_library_path.filename().replace_extension(".vst3");
            const fs::path windows_bundle_home =
                windows_library_path.parent_path().parent_path().parent_path();
            if (equals_case_insensitive(windows_bundle_home.filename().string(),
                                        win_module_name.string())) {
                return windows_bundle_home;
            } else {
                return windows_library_path;
            }
        } break;
        default:
            throw std::runtime_error("How did you manage to get this?");
            break;
    }
}

std::variant<OverridenWinePrefix, fs::path, DefaultWinePrefix> find_wine_prefix(
    fs::path windows_plugin_path) {
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    if (const auto prefix = getenv("WINEPREFIX")) {
        return OverridenWinePrefix{prefix};
    }

    const std::optional<fs::path> dosdevices_dir = find_dominating_file(
        "dosdevices", windows_plugin_path, fs::is_directory);
    if (!dosdevices_dir) {
        return DefaultWinePrefix{};
    }

    return dosdevices_dir->parent_path();
}

bool equals_case_insensitive(const std::string& a, const std::string& b) {
    return std::equal(a.begin(), a.end(), b.begin(),
                      [](const char& a_char, const char& b_char) {
                          return std::tolower(a_char) == std::tolower(b_char);
                      });
}

std::string join_quoted_strings(std::vector<std::string>& strings) {
    bool is_first = true;
    std::ostringstream joined_strings{};
    for (const auto& option : strings) {
        joined_strings << (is_first ? "'" : ", '") << option << "'";
        is_first = false;
    }

    return joined_strings.str();
}

std::string create_logger_prefix(const fs::path& endpoint_base_dir) {
    // Use the name of the base directory used for our sockets as the logger
    // prefix, but strip the `yabridge-` part since that's redundant
    std::string endpoint_name = endpoint_base_dir.filename().string();

    constexpr std::string_view socket_prefix("yabridge-");
    assert(endpoint_name.starts_with(socket_prefix));
    endpoint_name = endpoint_name.substr(socket_prefix.size());

    return "[" + endpoint_name + "] ";
}

fs::path find_vst_host(const ghc::filesystem::path& this_plugin_path,
                       LibArchitecture plugin_arch,
                       bool use_plugin_groups) {
    auto host_name = use_plugin_groups ? yabridge_group_host_name
                                       : yabridge_individual_host_name;
    if (plugin_arch == LibArchitecture::dll_32) {
        host_name = use_plugin_groups ? yabridge_group_host_name_32bit
                                      : yabridge_individual_host_name_32bit;
    }

    // If our `.so` file is a symlink, then search for the host in the directory
    // of the file that symlink points to
    fs::path host_path =
        fs::canonical(this_plugin_path).remove_filename() / host_name;
    if (fs::exists(host_path)) {
        return host_path;
    }

    if (const std::optional<fs::path> vst_host_path =
            search_in_path(get_augmented_search_path(), host_name)) {
        return *vst_host_path;
    } else {
        throw std::runtime_error("Could not locate '" + std::string(host_name) +
                                 "'");
    }
}

ghc::filesystem::path generate_group_endpoint(
    const std::string& group_name,
    const ghc::filesystem::path& wine_prefix,
    const LibArchitecture architecture) {
    std::ostringstream socket_name;
    socket_name << "yabridge-group-" << group_name << "-"
                << std::to_string(
                       std::hash<std::string>{}(wine_prefix.string()))
                << "-";
    switch (architecture) {
        case LibArchitecture::dll_32:
            socket_name << "x32";
            break;
        case LibArchitecture::dll_64:
            socket_name << "x64";
            break;
    }
    socket_name << ".sock";

    return get_temporary_directory() / socket_name.str();
}

std::vector<fs::path> get_augmented_search_path() {
    // HACK: `std::locale("")` would return the current locale, but this
    //       overload is implementation specific, and libstdc++ returns an error
    //       when this happens and one of the locale variables (or `LANG`) is
    //       set to a locale that doesn't exist. Because of that, you should use
    //       the default constructor instead which does fall back gracefully
    //       when using an invalid locale. Boost.Process sadly doesn't seem to
    //       do this, so some intervention is required. We can remove this once
    //       the PR linked below is merged into Boost proper and included in
    //       most distro's copy of Boost (which will probably take a while):
    //
    //       https://svn.boost.org/trac10/changeset/72855
    //
    //       https://github.com/boostorg/process/pull/179
    // FIXME: As mentioned above, we did this in the past to work around a
    //        Boost.Process bug. Since we no longer use Boost.Process, we can
    //        technically get rid of this, but we could also leave it in place
    //        since this may still cause other crashes for the user if we don't
    //        do it.
    try {
        std::locale("");
    } catch (const std::runtime_error&) {
        // We normally avoid modifying the current process' environment and
        // instead use `boost::process::environment` to only modify the
        // environment of launched child processes, but in this case we do need
        // to fix this
        // TODO: We don't have access to the logger here, so we cannot yet
        //       properly print the message inform the user that their locale is
        //       broken when this happens
        std::cerr << std::endl;
        std::cerr << "WARNING: Your locale is broken. Yabridge was kind enough "
                     "to monkey patch it for you in this DAW session, but you "
                     "should probably take a look at it ;)"
                  << std::endl;
        std::cerr << std::endl;

        setenv("LC_ALL", "C", true);  // NOLINT(concurrency-mt-unsafe)
    }

    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    const char* path_env = getenv("PATH");
    assert(path_env);

    std::vector<fs::path> search_path = split_path(path_env);

    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    if (const char* xdg_data_home = getenv("XDG_DATA_HOME")) {
        search_path.push_back(fs::path(xdg_data_home) / "yabridge");
        // NOLINTNEXTLINE(concurrency-mt-unsafe)
    } else if (const char* home_directory = getenv("HOME")) {
        search_path.push_back(fs::path(home_directory) / ".local" / "share" /
                              "yabridge");
    }

    return search_path;
}

Configuration load_config_for(const fs::path& yabridge_path) {
    // First find the closest `yabridge.tmol` file for the plugin, falling back
    // to default configuration settings if it doesn't exist
    const std::optional<fs::path> config_file =
        find_dominating_file("yabridge.toml", yabridge_path);
    if (!config_file) {
        return Configuration();
    }

    return Configuration(*config_file, yabridge_path);
}

bool send_notification(const std::string& title,
                       const std::string body,
                       bool append_origin) {
    // I think there's a zero chance that we're going to call this function with
    // anything that even somewhat resembles HTML, but we should still do a
    // basic XML escape anyways.
    std::ostringstream formatted_body;
    formatted_body << xml_escape(body);

    // If possible, append the path to this library file to the message.
    if (append_origin) {
        try {
            const fs::path this_library = get_this_file_location();
            formatted_body << "\n"
                           << "Source: <a href=\"file://"
                           << url_encode_path(
                                  this_library.parent_path().string())
                           << "\">"
                           << xml_escape(this_library.filename().string())
                           << "</a>";
        } catch (const std::system_error&) {
            // I don't think this can fail in the way we're using it, but the
            // last thing we want is our notification informing the user of an
            // exception to trigger another exception
        }
    }

    Process process("notify-send");
    process.arg("--urgency=normal");
    process.arg("--app-name=yabridge");
    process.arg(title);
    process.arg(formatted_body.str());

    // We will have printed the message to the terminal anyways, so if the user
    // doesn't have libnotify installed we'll just fail silently
    const auto result = process.spawn_get_status();
    return std::visit(
        overload{
            [](int status) -> bool { return status == EXIT_SUCCESS; },
            [](const Process::CommandNotFound&) -> bool { return false; },
            [](const std::error_code&) -> bool { return false; },
        },
        result);
}
