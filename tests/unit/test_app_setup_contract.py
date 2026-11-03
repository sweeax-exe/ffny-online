"""The app runs setup/engine.py itself (src/app/app_setup.cpp, app_update.cpp): every option it
passes must be one the engine takes, and every progress message its stage list waits for must be
one the engine sends, or a renamed message would leave the progress screen stuck on a step."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
APP = (ROOT / 'src/app/app_setup.cpp').read_text(encoding='utf-8')
UPDATE = (ROOT / 'src/app/app_update.cpp').read_text(encoding='utf-8')
ENGINE = (ROOT / 'setup/engine.py').read_text(encoding='utf-8')


def engine_options():
    return set(re.findall(r"add_argument\('(--[a-z-]+)'", ENGINE))


def test_every_option_the_app_passes_is_an_engine_option():
    # The engine's command line: spawn_engine's own, start()'s, and app_update.cpp's (not xcode-select's).
    spawn = APP[APP.index('bool spawn_engine('):APP.index('std::vector<std::string> env_text')]
    operation = APP[APP.index('std::vector<std::string> args = {"--bundled-only"'):APP.index('if (!spawn_engine(args')]
    update = ''.join(re.findall(r'spawn_engine\(\{(.*?)\}', UPDATE, re.S))
    passed = set(re.findall(r'"(--[a-z-]+)"', spawn + operation + update))
    assert {'--payload', '--operation', '--install-dir', '--data-dir', '--dump', '--status-file', '--cancel-file',
            '--log', '--bundled-only', '--no-shortcuts', '--install-prerequisites', '--check-app-update',
            '--stage-app', '--stage-dir'} <= passed
    assert passed - engine_options() == set()


def test_the_windows_build_tools_check_is_an_engine_option():
    # The app asks before setup starts whether Microsoft's C++ Build Tools are installed.
    check = APP[APP.index('void start_tools_check()'):APP.index('void poll_tools_check()')]
    assert set(re.findall(r'"(--[a-z-]+)"', check)) == {'--payload', '--check-toolchain'}
    assert '--check-toolchain' in engine_options()


def test_the_operations_the_app_starts_are_engine_operations():
    choices = re.search(r"'--operation', choices=\[([^\]]*)\]", ENGINE).group(1)
    operations = set(re.findall(r"'([a-z]+)'", choices))
    started = set(re.findall(r'start\("([a-z]+)"', APP))
    assert started == {'install', 'repair', 'rebuild', 'update'}
    assert started <= operations


def test_every_stage_prefix_is_a_message_the_engine_sends():
    table = APP[APP.index('const Stage k_stages[]'):APP.index('const int k_stage_count')]
    prefixes = re.findall(r'"([^"]+)"', table)
    names = re.findall(r'\{"([^"]+)", \{', table)
    for prefix in (p for p in prefixes if p not in names):
        if prefix in ('Analysis ', 'Lift ', 'Build '):
            # The pipeline's stages are reported as "<label> in progress" or "<label> already verified".
            assert "('" + prefix.strip() + "'," in ENGINE, prefix
        else:
            assert re.search(r"self\.event\(f?['\"]" + re.escape(prefix), ENGINE), prefix
