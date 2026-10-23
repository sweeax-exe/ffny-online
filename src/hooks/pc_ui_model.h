// Launcher-only data helpers. No renderer or guest state; usable by native fixtures.
#pragma once
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace pcui
{
namespace fs = std::filesystem;
inline std::string trim(std::string s)
{
    const auto a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}
inline bool pack_name(const std::string &s)
{
    return !s.empty() && s != "." && s != ".." && std::all_of(s.begin(), s.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '_' || c == '-' || c == '.';
    });
}
inline std::vector<std::string> split_packs(const std::string &text)
{
    std::vector<std::string> out;
    std::istringstream in(text);
    std::string s;
    while (std::getline(in, s, ';'))
    {
        s = trim(s);
        if (!s.empty())
        {
            // Preserve the last occurrence: the loader gives later packs precedence.
            auto previous = std::find(out.begin(), out.end(), s);
            if (previous != out.end())
                out.erase(previous);
            out.push_back(s);
        }
    }
    return out;
}
struct Pack
{
    std::string name, problem;
    bool selected = false;
};
inline std::string manifest_problem(const fs::path &folder)
{
    std::ifstream in(folder / "manifest.ini");
    if (!in)
        return "Missing manifest.ini";
    std::string line, section;
    bool schema = false;
    // Only the small header is needed for inventory; the loader validates image entries.
    for (int n = 0; n < 128 && std::getline(in, line); n++)
    {
        if (line.size() > 2048)
            return "Manifest header line too long";
        if (line.compare(0, 3, "\xef\xbb\xbf") == 0)
            line.erase(0, 3);
        line = trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#')
            continue;
        if (line[0] == '[')
        {
            section = line;
            if (schema && section != "[pack]")
                break;
        }
        auto eq = line.find('=');
        if (section == "[pack]" && eq != std::string::npos && trim(line.substr(0, eq)) == "schema")
            schema = trim(line.substr(eq + 1)) == "1";
    }
    return schema ? "" : "Unsupported manifest schema (expected 1)";
}
inline std::vector<Pack> inventory(const fs::path &mods, const std::string &chosen)
{
    std::vector<Pack> out;
    for (const auto &name : split_packs(chosen))
        out.push_back({name, "", true});
    std::error_code ec;
    std::vector<std::string> available;
    for (fs::directory_iterator it(mods, ec), end; !ec && it != end; it.increment(ec))
    {
        std::error_code entry_error;
        if (it->is_directory(entry_error))
            available.push_back(it->path().filename().u8string());
    }
    std::sort(available.begin(), available.end());
    for (const auto &name : available)
        if (std::none_of(out.begin(), out.end(), [&](const Pack &p) { return p.name == name; }))
            out.push_back({name, "", false});
    for (auto &p : out)
    {
        if (!pack_name(p.name))
            p.problem = "Invalid pack folder name";
        else
        {
            std::error_code folder_error;
            auto folder = mods / fs::u8path(p.name);
            p.problem = fs::is_directory(folder, folder_error) ? manifest_problem(folder) : "Not installed";
        }
    }
    return out;
}
inline std::string selected_packs(const std::vector<Pack> &packs)
{
    std::string result;
    for (const auto &p : packs)
        if (p.selected)
        {
            if (!result.empty())
                result += ';';
            result += p.name;
        }
    return result;
}
// Bounded, refresh-time metadata only. No image bytes are loaded by the launcher.
inline std::set<std::string> pack_texture_ids(const fs::path &folder)
{
    std::ifstream input(folder / "manifest.ini");
    std::string line, section;
    std::set<std::string> ids;
    for (unsigned n=0; n<131072 && std::getline(input,line); n++) {
        if (line.size()>2048) break;
        line=trim(line);
        if (!line.empty() && line[0]=='[') section=line;
        if (section!="[textures]") continue;
        auto eq=line.find('=');
        if (eq==std::string::npos) continue;
        auto id=trim(line.substr(0,eq));
        if (id.size()!=64 || !std::all_of(id.begin(),id.end(),[](unsigned char c){return std::isxdigit(c);})) continue;
        std::transform(id.begin(),id.end(),id.begin(),[](unsigned char c){return (char)std::tolower(c);});
        ids.insert(id);
        if (ids.size()>=65536) break;
    }
    return ids;
}
inline std::map<std::string,std::string> pack_conflicts(const fs::path &mods,const std::vector<Pack> &packs)
{
    std::map<std::string,std::set<std::string>> keys;
    std::map<std::string,std::string> winner, result;
    for (const auto &pack:packs) if (pack.selected && pack.problem.empty()) {
        keys[pack.name]=pack_texture_ids(mods/fs::u8path(pack.name));
        for (const auto &key:keys[pack.name]) winner[key]=pack.name;
    }
    for (const auto &pack:keys) {
        std::map<std::string,unsigned> counts;
        for (const auto &key:pack.second) if (winner[key]!=pack.first) counts[winner[key]]++;
        std::string text;
        for (const auto &count:counts) {
            if (!text.empty()) text+="; ";
            text+=std::to_string(count.second)+" texture(s) overridden by "+count.first;
        }
        result[pack.first]=text;
    }
    return result;
}
inline bool aspect_next_launch(int before, int after)
{
    return before != after && (before >= 2 || after >= 2);
}
} // namespace pcui
