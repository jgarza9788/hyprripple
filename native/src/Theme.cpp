#include "Theme.hpp"

#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <unordered_map>

namespace {
    // Omarchy blue, used if the theme can't be read at all.
    const CHyprColor FALLBACK{0x7a / 255.0, 0xa2 / 255.0, 0xf7 / 255.0, 1.0};

    std::unordered_map<std::string, std::string> s_colors;
    struct timespec                              s_mtime{};
    ino_t                                        s_inode = 0;
    std::string                                  s_loadedPath;

    std::string trim(std::string s) {
        auto notSpace = [](unsigned char c) { return !std::isspace(c); };
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
        s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
        return s;
    }

    std::optional<uint32_t> parseHex(std::string_view hex) {
        if (hex.empty() || hex.size() > 8)
            return std::nullopt;
        uint32_t out = 0;
        for (char c : hex) {
            out <<= 4;
            if (c >= '0' && c <= '9')
                out |= c - '0';
            else if (c >= 'a' && c <= 'f')
                out |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F')
                out |= c - 'A' + 10;
            else
                return std::nullopt;
        }
        return out;
    }

    CHyprColor rgba(uint32_t r, uint32_t g, uint32_t b, uint32_t a) {
        return CHyprColor{r / 255.0, g / 255.0, b / 255.0, a / 255.0};
    }

    // Minimal colors.toml reader: `key = "value"` lines, comments and tables ignored.
    void reloadIfChanged() {
        const std::string path = Theme::colorsPath();
        struct stat       st{};
        if (stat(path.c_str(), &st) != 0) {
            s_colors.clear();
            s_loadedPath.clear();
            return;
        }

        if (path == s_loadedPath && st.st_ino == s_inode && st.st_mtim.tv_sec == s_mtime.tv_sec && st.st_mtim.tv_nsec == s_mtime.tv_nsec)
            return;

        s_colors.clear();
        std::ifstream file(path);
        std::string   line;
        while (std::getline(file, line)) {
            if (auto hash = line.find('#'); hash != std::string::npos) {
                // a '#' inside quotes is a color, not a comment
                const auto quote = line.find('"');
                if (quote == std::string::npos || hash < quote)
                    line = line.substr(0, hash);
            }
            const auto eq = line.find('=');
            if (eq == std::string::npos)
                continue;
            std::string key = trim(line.substr(0, eq));
            std::string val = trim(line.substr(eq + 1));
            if (val.size() >= 2 && (val.front() == '"' || val.front() == '\'') && val.back() == val.front())
                val = val.substr(1, val.size() - 2);
            if (!key.empty() && !val.empty())
                s_colors[key] = val;
        }

        s_loadedPath = path;
        s_inode      = st.st_ino;
        s_mtime      = st.st_mtim;
    }
}

std::string Theme::colorsPath() {
    const char*       home  = std::getenv("HOME");
    const std::string base  = home ? home : "";
    const std::string state = base + "/.local/state/omarchy/current/theme/colors.toml";
    struct stat       st{};
    if (stat(state.c_str(), &st) == 0)
        return state;
    return base + "/.config/omarchy/current/theme/colors.toml";
}

std::optional<CHyprColor> Theme::parseLiteral(const std::string& raw) {
    std::string value = trim(raw);
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return std::tolower(c); });

    // rgba(rrggbbaa) / rgb(rrggbb), Hyprland style
    if (value.starts_with("rgba(") && value.ends_with(")"))
        value = "#" + value.substr(5, value.size() - 6);
    else if (value.starts_with("rgb(") && value.ends_with(")"))
        value = "#" + value.substr(4, value.size() - 5);

    if (value.starts_with("0x") && value.size() == 10) { // 0xaarrggbb
        auto v = parseHex(std::string_view{value}.substr(2));
        if (!v)
            return std::nullopt;
        return rgba((*v >> 16) & 0xff, (*v >> 8) & 0xff, *v & 0xff, (*v >> 24) & 0xff);
    }

    if (!value.starts_with("#"))
        return std::nullopt;

    const std::string_view hex = std::string_view{value}.substr(1);
    auto                   v   = parseHex(hex);
    if (!v)
        return std::nullopt;

    switch (hex.size()) {
        case 3: return rgba(((*v >> 8) & 0xf) * 17, ((*v >> 4) & 0xf) * 17, (*v & 0xf) * 17, 255);
        case 6: return rgba((*v >> 16) & 0xff, (*v >> 8) & 0xff, *v & 0xff, 255);
        case 8: return rgba((*v >> 24) & 0xff, (*v >> 16) & 0xff, (*v >> 8) & 0xff, *v & 0xff);
        default: return std::nullopt;
    }
}

CHyprColor Theme::resolve(const std::string& setting) {
    if (auto literal = parseLiteral(setting))
        return *literal;

    reloadIfChanged();

    const std::string key = setting.empty() ? "accent" : trim(setting);
    if (auto it = s_colors.find(key); it != s_colors.end())
        if (auto c = parseLiteral(it->second))
            return *c;

    if (auto it = s_colors.find("accent"); it != s_colors.end())
        if (auto c = parseLiteral(it->second))
            return *c;

    return FALLBACK;
}
