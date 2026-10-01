#include "Theme.hpp"
#include "Logic.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <unordered_map>

namespace {
    // Omarchy blue, used if the theme can't be read at all.
    const CHyprColor FALLBACK{0x7a / 255.0, 0xa2 / 255.0, 0xf7 / 255.0, 1.0};

    constexpr off_t MAX_COLORS_FILE = 64 * 1024;

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
        s_loadedPath = path;
        s_inode      = st.st_ino;
        s_mtime      = st.st_mtim;

        // Check the file we actually opened (fstat on the fd), not the path,
        // so it can't be swapped between the check and the read. A theme's
        // colors.toml is a few hundred bytes: refuse anything that isn't a
        // modest regular file, and never block on a FIFO.
        const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK | O_NOCTTY);
        if (fd < 0)
            return;
        struct stat fst{};
        std::string content;
        if (fstat(fd, &fst) == 0 && S_ISREG(fst.st_mode) && fst.st_size <= MAX_COLORS_FILE) {
            content.resize((size_t)fst.st_size);
            size_t got = 0;
            while (got < content.size()) {
                const ssize_t n = read(fd, content.data() + got, content.size() - got);
                if (n <= 0)
                    break;
                got += (size_t)n;
            }
            content.resize(got);
        }
        close(fd);

        std::istringstream file(content);
        std::string        line;
        int           lines = 0;
        while (std::getline(file, line) && ++lines <= 512) {
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
            if (Logic::isColorKey(key) && !val.empty() && val.size() <= 32)
                s_colors[key] = val;
        }
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
    auto c = Logic::parseColorLiteral(raw);
    if (!c)
        return std::nullopt;
    return CHyprColor{(float)(*c)[0], (float)(*c)[1], (float)(*c)[2], (float)(*c)[3]};
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
