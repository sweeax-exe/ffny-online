#pragma once
#ifdef __cplusplus
#include <map>
#include <string>
#include <vector>
namespace pcui {
struct ModRow {
    std::string id, name, authors, description, installed, latest, load_after, channel;
    unsigned long long bytes = 0;
    bool compatible = false, revoked = false, update = false;
};
struct ManagedMod { std::string folder, name, version; bool rollback = false; };
struct ModView {
    std::string message, catalogue;
    std::vector<ModRow> catalogue_mods;
    std::map<std::string, ManagedMod> installed;
};
bool mod_start(const std::string &command, const std::string &id = "");
bool mod_poll(); // true after a job completed; caller refreshes local inventory
bool mod_busy();
bool mod_prerelease();
void mod_set_prerelease(bool enabled);
void mod_cancel();
const ModView &mod_view();
const std::string &mod_status();
bool mod_recovery_needed();
}
extern "C" {
#endif
/* Acquire a shared data-root lock for the lifetime of the game. Refuse an
 * incomplete transaction or an active installer, including --no-launcher. */
int pc_mods_game_lock(void);
int pc_mods_needs_recovery(void);
#ifdef __cplusplus
}
#endif
