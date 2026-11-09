"""FFNY Online: upstream Def Jam Recompiled v1.1.1 tests for parts this fork has not taken in yet.

The upstream merge's stage 1 (docs/research/upstream-v1.1.1-merge.md) brought the game runtime
only. These test upstream's game-free app, its setup and update path, the Steam shortcuts, its
website, custom songs and the settings catalogue the app shares, none of which is built here. Each
stage that takes a part in removes its tests from this list.
"""

collect_ignore = [
    # stage 2: upstream's app (src/app) and the settings catalogue it shares with the game
    'test_app_package.py',
    'test_app_setup_contract.py',
    'test_app_updates.py',
    'test_appimage.py',
    'test_experience_presets.py',
    'test_setup_handoff.py',
    'test_steam_shortcuts.py',
    'test_ui_keys_native.py',
    # stage 3: upstream's setup, Linux toolchain and website
    'test_build_site.py',
    'test_linux_toolchain.py',
    # stage 4: custom songs and the Beatbox
    'test_song_convert.py',
    'test_songs.py',
]

# Single tests of upstream files this fork keeps as its own for now.
DEFERRED_TESTS = {
    # stage 2: the overlay's cheat page model (upstream's pc_ui_model.h; FFNY keeps its own)
    'test_cheats_native.py::test_fighter_roster_from_fighter_xml',
}


def pytest_collection_modifyitems(config, items):
    import pytest
    for item in items:
        if '::'.join(item.nodeid.split('/')[-1:]) in DEFERRED_TESTS:
            item.add_marker(pytest.mark.skip(reason='upstream merge: deferred to a later stage (conftest.py)'))
