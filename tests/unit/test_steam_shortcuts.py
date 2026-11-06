"""Add to Steam for the app (setup/steam_shortcuts.py, design §7): the shortcut written into a stand-in
Steam folder's shortcuts.vdf, the artwork, and Steam closed around the change. Synthetic data only;
nothing here reads or writes a real Steam."""
import importlib.util
import json
from pathlib import Path
import shutil
import struct

import pytest

ROOT = Path(__file__).resolve().parents[2]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


steam = load('test_steam_shortcuts_module', ROOT / 'setup/steam_shortcuts.py')
engine = load('test_steam_shortcuts_engine', ROOT / 'setup/engine.py')

PROGRAM = Path(r'C:\Users\Player\AppData\Local\Programs\DefJamRecompiled\DefJamRecompiled.exe')
INSTALL = r'C:\Users\Player\AppData\Local\DefJamRecompiled\app'
ACCOUNT = 1234567


def other_shortcut(appid, name, exe):
    """An entry as Steam writes it, with a tag, a 64-bit value and a float, to prove bytes are kept."""
    return (b'\x02appid\x00' + struct.pack('<I', appid) + b'\x01AppName\x00' + name.encode() + b'\x00'
            + b'\x01Exe\x00"' + exe.encode() + b'"\x00' + b'\x07Big\x00' + struct.pack('<Q', 2 ** 40 + 7)
            + b'\x03Volume\x00' + struct.pack('<f', 0.5)
            + b'\x00tags\x00\x010\x00Favorites\x00\x08' + b'\x08')


def steam_folder(tmp_path, *bodies, users=((ACCOUNT, 'Player', '1'),)):
    root = tmp_path / 'Steam'
    (root / 'config').mkdir(parents=True)
    lines = ['"users"', '{']
    for account, name, recent in users:
        lines += ['\t"%d"' % (steam.STEAMID64_BASE + account), '\t{', '\t\t"AccountName"\t\t"%s"' % name.lower(),
                  '\t\t"PersonaName"\t\t"%s"' % name, '\t\t"MostRecent"\t\t"%s"' % recent,
                  '\t\t"Timestamp"\t\t"%d"' % (1700000000 + account % 1000), '\t}']
    (root / 'config/loginusers.vdf').write_text('\n'.join(lines + ['}']) + '\n', encoding='utf-8')
    for account, _, _ in users:
        (root / 'userdata' / str(account) / 'config').mkdir(parents=True)
    if bodies:
        (root / 'userdata' / str(users[0][0]) / 'config/shortcuts.vdf').write_bytes(steam.join_shortcuts(list(bodies)))
    return root


def art(tmp_path):
    folders = [tmp_path / 'payload/source/branding/fan/steam', tmp_path / 'payload/source/branding/steam']
    for folder in folders:
        folder.mkdir(parents=True)
    for name in ('portrait.png', 'wide.png', 'logo.png'):
        (folders[0] / name).write_bytes(b'fan ' + name.encode())
    for name in ('hero.png', 'icon.png', 'portrait.png'):
        (folders[1] / name).write_bytes(b'plain ' + name.encode())
    return folders


class FakeSteam:
    """Steam's process as add and remove see it: running or not, and what they asked of it."""

    def __init__(self, running):
        self.up = running
        self.calls = []

    def running(self, root):
        return [42] if self.up else []

    def close(self, root):
        self.calls.append('close')
        self.up = False

    def start(self, root):
        self.calls.append('start')
        self.up = True


def entries(root, account=ACCOUNT):
    bodies, _ = steam.split_shortcuts((root / 'userdata' / str(account) / 'config/shortcuts.vdf').read_bytes())
    return bodies


def test_the_shortcut_is_added_after_the_others_which_keep_their_bytes(tmp_path):
    other = other_shortcut(0x9000_0001, 'Other game', r'C:\Games\other.exe')
    root = steam_folder(tmp_path, other)
    before = (root / 'userdata' / str(ACCOUNT) / 'config/shortcuts.vdf').read_bytes()
    record = tmp_path / 'user/steam.json'
    messages = []
    result = steam.add(root, PROGRAM, INSTALL, art(tmp_path), record, event=messages.append,
                       running=lambda r: [])
    bodies = entries(root)
    assert bodies[0] == other                                       # untouched, byte for byte
    ours = steam.fields(bodies[1])
    appid = steam.shortcut_appid('"%s"' % PROGRAM)
    assert result['appid'] == appid and steam.field(ours, 'appid') == appid and appid & 0x80000000
    assert ours['AppName'] == 'Def Jam Recompiled' and ours['Exe'] == '"%s"' % PROGRAM
    assert ours['StartDir'] == '"%s"' % PROGRAM.parent
    assert ours['LaunchOptions'] == '--install "%s"' % INSTALL          # the launcher unless Skip launcher
    assert ours['AllowOverlay'] == 1 and ours['tags'] == {}
    grid = root / 'userdata' / str(ACCOUNT) / 'config/grid'
    assert ours['icon'] == str(grid / ('%d_icon.png' % appid))
    # The fan set first; the plain set fills the hero and icon. Steam's own file is backed up.
    assert (grid / ('%dp.png' % appid)).read_bytes() == b'fan portrait.png'
    assert (grid / ('%d.png' % appid)).read_bytes() == b'fan wide.png'
    assert (grid / ('%d_logo.png' % appid)).read_bytes() == b'fan logo.png'
    assert (grid / ('%d_hero.png' % appid)).read_bytes() == b'plain hero.png'
    assert (grid / ('%d_icon.png' % appid)).read_bytes() == b'plain icon.png'
    assert (root / 'userdata' / str(ACCOUNT) / 'config' / steam.BACKUP).read_bytes() == before
    saved = json.loads(record.read_text())
    assert saved['appid'] == appid and len(saved['art']) == 5 and set(saved['art_hashes']) == set(saved['art'])
    assert "Player's Steam library" in messages[0]


def test_adding_again_keeps_one_shortcut_and_the_players_own_art(tmp_path):
    root = steam_folder(tmp_path)                                    # no shortcuts.vdf yet
    folders, record = art(tmp_path), tmp_path / 'steam.json'
    steam.add(root, PROGRAM, INSTALL, folders, record, running=lambda r: [])
    appid = steam.shortcut_appid('"%s"' % PROGRAM)
    hero = root / 'userdata' / str(ACCOUNT) / 'config/grid' / ('%d_hero.png' % appid)
    hero.write_bytes(b'my own hero')
    messages = []
    steam.add(root, PROGRAM, INSTALL, folders, record, event=messages.append, running=lambda r: [])
    assert len(entries(root)) == 1
    assert hero.read_bytes() == b'my own hero' and any('your own artwork' in m for m in messages)
    assert str(hero) not in json.loads(record.read_text())['art']


def test_adding_again_takes_the_old_play_out_of_the_shortcut_and_nothing_else(tmp_path):
    other = other_shortcut(0x9000_0001, 'Other game', r'C:\Games\other.exe')
    appid = steam.shortcut_appid('"%s"' % PROGRAM)
    old = steam.shortcut_body(appid, PROGRAM, PROGRAM.parent, 'icon.png', '--play --install "%s"' % INSTALL)
    old = old.replace(b'\x00tags\x00\x08', b'\x00tags\x00\x010\x00Fighting\x00\x08')  # the player's own tag
    root = steam_folder(tmp_path, other, old)
    record = tmp_path / 'steam.json'
    assert steam.outdated(steam.fields(old))
    messages = []
    steam.add(root, PROGRAM, INSTALL, art(tmp_path), record, event=messages.append, running=lambda r: [])
    bodies = entries(root)
    assert len(bodies) == 2 and bodies[0] == other
    assert bodies[1] == old.replace(b'--play --install', b'--install')       # only the option changed
    assert not steam.outdated(steam.fields(bodies[1])) and any('Updated' in m for m in messages)
    backup = root / 'userdata' / str(ACCOUNT) / 'config' / steam.BACKUP
    assert entries_of(backup)[1] == old
    messages.clear()
    steam.add(root, PROGRAM, INSTALL, art(tmp_path / 'again'), record, event=messages.append, running=lambda r: [])
    assert entries(root)[1] == bodies[1] and any('already' in m for m in messages)


def test_status_reports_a_shortcut_that_still_skips_the_launcher(tmp_path, monkeypatch):
    appid = steam.shortcut_appid('"%s"' % PROGRAM)
    root = steam_folder(tmp_path, steam.shortcut_body(appid, PROGRAM, PROGRAM.parent, '', '--play --install "x"'))
    answer = steam.status(root, PROGRAM, running=lambda r: [])
    assert answer['added'] and answer['outdated']
    steam.add(root, PROGRAM, INSTALL, art(tmp_path), tmp_path / 'steam.json', running=lambda r: [])
    answer = steam.status(root, PROGRAM, running=lambda r: [])
    assert answer['added'] and not answer['outdated']


def entries_of(vdf):
    return steam.split_shortcuts(Path(vdf).read_bytes())[0]


def test_a_shortcut_steam_made_without_an_appid_is_recognised(tmp_path):
    legacy = (b'\x01AppName\x00Def Jam\x00\x01Exe\x00"' + str(PROGRAM).encode() + b'"\x00\x00tags\x00\x08\x08')
    root = steam_folder(tmp_path, legacy)
    result = steam.add(root, PROGRAM, INSTALL, art(tmp_path), tmp_path / 'steam.json', running=lambda r: [])
    assert len(entries(root)) == 1 and entries(root)[0] == legacy
    assert result['appid'] == steam.shortcut_appid('"%s"' % PROGRAM, 'Def Jam')


def test_remove_takes_out_only_our_shortcut_and_the_art_we_placed(tmp_path):
    other = other_shortcut(0x9000_0001, 'Other game', r'C:\Games\other.exe')
    root = steam_folder(tmp_path, other)
    folders, record = art(tmp_path), tmp_path / 'steam.json'
    steam.add(root, PROGRAM, INSTALL, folders, record, running=lambda r: [])
    appid = steam.shortcut_appid('"%s"' % PROGRAM)
    grid = root / 'userdata' / str(ACCOUNT) / 'config/grid'
    (grid / ('%d_hero.png' % appid)).write_bytes(b'my own hero')     # the player's since
    (grid / '2415919105p.png').write_bytes(b'the other game')          # another game's art
    assert steam.remove(root, PROGRAM, folders, record, running=lambda r: []) == {'removed': 1}
    assert entries(root) == [other]
    assert sorted(p.name for p in grid.iterdir()) == ['2415919105p.png', '%d_hero.png' % appid]
    assert not record.exists()
    assert steam.remove(root, PROGRAM, folders, record, running=lambda r: []) == {'removed': 0}


def test_a_running_steam_is_refused_unless_it_may_be_closed_and_is_started_again(tmp_path):
    root = steam_folder(tmp_path, other_shortcut(1, 'Other', r'C:\o.exe'))
    vdf = root / 'userdata' / str(ACCOUNT) / 'config/shortcuts.vdf'
    before = vdf.read_bytes()
    folders, record = art(tmp_path), tmp_path / 'steam.json'
    fake = FakeSteam(running=True)
    with pytest.raises(steam.SteamError) as refused:
        steam.add(root, PROGRAM, INSTALL, folders, record, running=fake.running, closer=fake.close, starter=fake.start)
    assert refused.value.code == 4 and vdf.read_bytes() == before and fake.calls == []

    order = []
    real_write = steam.write_atomically

    def closer(r):
        fake.close(r)
        order.append('closed')

    def write(path, data):
        assert not fake.up, 'Steam must be closed while its file is written'
        order.append('written ' + Path(path).name)
        real_write(path, data)

    steam.write_atomically = write
    try:
        steam.add(root, PROGRAM, INSTALL, folders, record, close=True, running=fake.running, closer=closer,
                  starter=fake.start)
    finally:
        steam.write_atomically = real_write
    assert order[:2] == ['closed', 'written shortcuts.vdf'] and fake.calls == ['close', 'start'] and fake.up


def test_steam_is_started_again_even_when_the_change_fails(tmp_path, monkeypatch):
    root = steam_folder(tmp_path)
    fake = FakeSteam(running=True)
    monkeypatch.setattr(steam, 'write_atomically', lambda *a: (_ for _ in ()).throw(OSError('disk full')))
    with pytest.raises(OSError):
        steam.add(root, PROGRAM, INSTALL, art(tmp_path), tmp_path / 's.json', close=True,
                  running=fake.running, closer=fake.close, starter=fake.start)
    assert fake.calls == ['close', 'start']


def test_a_damaged_file_is_refused_before_steam_is_closed(tmp_path):
    root = steam_folder(tmp_path)
    vdf = root / 'userdata' / str(ACCOUNT) / 'config/shortcuts.vdf'
    vdf.write_bytes(b'\x00shortcuts\x00\x000\x00\x01AppName\x00broken')
    fake = FakeSteam(running=True)
    with pytest.raises(steam.SteamError, match='could not be read'):
        steam.add(root, PROGRAM, INSTALL, art(tmp_path), tmp_path / 's.json', close=True,
                  running=fake.running, closer=fake.close, starter=fake.start)
    assert fake.calls == [] and vdf.read_bytes().endswith(b'broken')


def test_the_account_is_the_one_that_signed_in_last(tmp_path):
    root = steam_folder(tmp_path, users=((111, 'Old', '0'), (222, 'Recent', '1')))
    assert steam.steam_user(root) == (root / 'userdata/222', 'Recent')
    (root / 'config/loginusers.vdf').unlink()
    with pytest.raises(steam.SteamError, match='Several Steam accounts'):
        steam.steam_user(root)
    (root / 'userdata/111').rename(root / 'userdata/0')                # Steam's anonymous folder
    assert steam.steam_user(root) == (root / 'userdata/222', '222')


def test_the_engine_answers_status_add_and_remove_as_json(tmp_path, monkeypatch):
    root = steam_folder(tmp_path)
    art(tmp_path)
    monkeypatch.setenv('DEFJAM_STEAM_ROOT', str(root))
    monkeypatch.setattr(steam, 'steam_processes', lambda r: [], raising=True)
    result = tmp_path / 'result.json'
    common = ['--payload', str(tmp_path / 'payload'), '--app-program', str(PROGRAM), '--install-dir', INSTALL,
              '--steam-record', str(tmp_path / 'steam.json'), '--steam-result', str(result)]
    # The engine loads its own copy of the module; on this platform it finds no running Steam either.
    assert engine.main(['--steam', 'status'] + common) == 0
    answer = json.loads(result.read_text())
    assert answer['steam'] == str(root) and answer['user'] == 'Player' and answer['added'] is False
    assert engine.main(['--steam', 'add'] + common) == 0
    answer = json.loads(result.read_text())
    assert answer['added'] is True and "Player's Steam library" in answer['messages'][0]
    assert engine.main(['--steam', 'remove'] + common) == 0
    assert json.loads(result.read_text())['added'] is False
    monkeypatch.setenv('DEFJAM_STEAM_ROOT', str(tmp_path / 'nowhere'))
    assert engine.main(['--steam', 'add'] + common) == 2
    assert 'Sign in to Steam' in json.loads(result.read_text())['message']


@pytest.mark.parametrize('platform', ['windows-x64', 'linux-x64'])
def test_add_to_steam_refreshes_owned_art_from_the_shipped_branding(tmp_path, monkeypatch, platform):
    """Exercise the app's engine command with the actual platform payload and PNGs."""
    Image = pytest.importorskip('PIL.Image')
    package = load('steam_branding_package', ROOT / 'scripts/package-setup.py')
    payload = tmp_path / 'payload'
    for source in (ROOT / 'branding').rglob('*'):
        name = source.relative_to(ROOT).as_posix()
        if source.is_file() and package.payload_wants(name, platform):
            target = payload / 'source' / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target)

    other = other_shortcut(0x9000_0001, 'Other game', r'C:\Games\other.exe')
    root = steam_folder(tmp_path, other)
    record = tmp_path / 'steam.json'
    # An earlier install owns this old artwork; adding again must refresh it.
    steam.add(root, PROGRAM, INSTALL, art(tmp_path / 'old'), record, running=lambda r: [])
    original_entries = entries(root)
    monkeypatch.setenv('DEFJAM_STEAM_ROOT', str(root))
    result = tmp_path / 'result.json'
    common = ['--payload', str(payload), '--app-program', str(PROGRAM), '--install-dir', INSTALL,
              '--steam-record', str(record), '--steam-result', str(result)]
    assert engine.main(['--steam', 'add'] + common) == 0
    assert json.loads(result.read_text())['added'] is True
    assert entries(root) == original_entries  # no duplicate and no shortcut option/tag changes
    appid = steam.shortcut_appid('"%s"' % PROGRAM)
    grid = root / 'userdata' / str(ACCOUNT) / 'config/grid'
    expected = {
        'p': ('fan/steam/portrait.png', (600, 900)),
        '': ('fan/steam/wide.png', (920, 430)),
        '_logo': ('fan/steam/logo.png', None),
        '_hero': ('steam/hero.png', (1920, 620)),
        '_icon': ('steam/icon.png', (256, 256)),
    }
    assert len(list(grid.glob('*.png'))) == 5
    for suffix, (source, size) in expected.items():
        placed = grid / f'{appid}{suffix}.png'
        assert placed.read_bytes() == (ROOT / 'branding' / source).read_bytes()
        with Image.open(placed) as image:
            image.load()
            assert image.format == 'PNG'
            if size:
                assert image.size == size
            else:
                assert image.width == 1280 and image.mode == 'RGBA'
    saved = json.loads(record.read_text())
    assert all(steam.digest(Path(path)) == digest for path, digest in saved['art_hashes'].items())

    hero = grid / f'{appid}_hero.png'
    hero.write_bytes(b'player-chosen artwork')
    assert engine.main(['--steam', 'add'] + common) == 0
    assert hero.read_bytes() == b'player-chosen artwork' and entries(root) == original_entries
    assert engine.main(['--steam', 'remove'] + common) == 0
    assert entries(root) == [other]
    assert list(grid.iterdir()) == [hero]  # retain custom art; remove everything this app owned


# ---------------------------------------------------------------- Linux (the AppImage, design §6, §7)

linux_only = pytest.mark.skipif(steam.IS_WINDOWS, reason='Linux Steam folders and processes')


@linux_only
def test_linux_finds_steam_through_its_link_or_the_usual_folder(tmp_path, monkeypatch):
    monkeypatch.setenv('HOME', str(tmp_path))
    monkeypatch.delenv('DEFJAM_STEAM_ROOT', raising=False)
    monkeypatch.delenv('XDG_DATA_HOME', raising=False)
    monkeypatch.setattr(steam.sys, 'platform', 'linux')
    assert steam.steam_root() is None
    real = tmp_path / '.local/share/Steam'
    real.mkdir(parents=True)
    (real / 'steam.sh').write_text('#!/bin/sh\n')
    assert steam.steam_root() == real.resolve()
    (tmp_path / '.steam').mkdir()
    (tmp_path / '.steam/steam').symlink_to(real)
    assert steam.steam_root() == real.resolve()


@linux_only
def test_linux_counts_only_the_steam_client_in_that_folder(tmp_path):
    root = tmp_path / 'Steam'
    client = root / 'ubuntu12_32/steam'
    client.parent.mkdir(parents=True)
    client.write_text('')
    proc = tmp_path / 'proc'
    for pid, exe, argv in ((101, client, [str(client)]), (102, Path('/usr/bin/python3'), ['python3', str(client)]),
                           (103, tmp_path / 'other/ubuntu12_32/steam', ['steam']), (104, Path('/bin/sh'), ['sh'])):
        (proc / str(pid)).mkdir(parents=True)
        (proc / str(pid) / 'exe').symlink_to(exe)
        (proc / str(pid) / 'cmdline').write_bytes(b'\0'.join(a.encode() for a in argv) + b'\0')
    (proc / 'self').mkdir()
    assert sorted(steam.steam_processes_linux(root, proc)) == [101, 102]


@linux_only
def test_game_mode_is_asked_of_steam_itself(tmp_path):
    proc = tmp_path / 'proc'
    for pid, argv, env in ((1, [b'steam', b'-steamdeck'], [b'XDG_CURRENT_DESKTOP=KDE']),
                           (2, [b'steam', b'-steamdeck', b'-gamepadui'], [b'XDG_CURRENT_DESKTOP=KDE']),
                           (3, [b'steam'], [b'XDG_CURRENT_DESKTOP=gamescope'])):
        (proc / str(pid)).mkdir(parents=True)
        (proc / str(pid) / 'cmdline').write_bytes(b'\0'.join(argv) + b'\0')
        (proc / str(pid) / 'environ').write_bytes(b'\0'.join(env) + b'\0')
    assert not steam.game_mode([1], proc) and steam.game_mode([2], proc) and steam.game_mode([3], proc)
    assert not steam.game_mode([4], proc)                    # gone meanwhile


@linux_only
def test_game_mode_never_closes_steam(tmp_path, monkeypatch):
    root = steam_folder(tmp_path)
    vdf = root / 'userdata' / str(ACCOUNT) / 'config/shortcuts.vdf'
    monkeypatch.setattr(steam, 'game_mode', lambda pids, proc=None: bool(pids))
    fake = FakeSteam(running=True)
    with pytest.raises(steam.SteamError, match='Desktop Mode'):
        steam.add(root, Path('/home/deck/Applications/DefJamRecompiled.AppImage'), '/home/deck/Games/App',
                  art(tmp_path), tmp_path / 's.json', close=True, running=fake.running, closer=fake.close,
                  starter=fake.start)
    assert fake.calls == [] and not vdf.exists()
    fake = FakeSteam(running=False)                     # Steam not running: nothing to close
    steam.add(root, Path('/home/deck/Applications/DefJamRecompiled.AppImage'), '/home/deck/Games/App',
              art(tmp_path / 'again'), tmp_path / 's.json', running=fake.running, closer=fake.close, starter=fake.start)
    assert [steam.field(steam.fields(b), 'LaunchOptions') for b in steam.read_shortcuts(vdf)[0]] == [
        '--install "/home/deck/Games/App"']


@linux_only
def test_steam_is_started_from_the_stand_in_folder_without_the_appimage_variables(tmp_path, monkeypatch):
    monkeypatch.setenv('DEFJAM_STEAM_ROOT', str(tmp_path))
    monkeypatch.setenv('APPIMAGE', '/home/deck/Applications/DefJamRecompiled.AppImage')
    monkeypatch.setenv('APPDIR', '/tmp/.mount_DefJam123')
    assert steam.steam_program(tmp_path) == str(tmp_path / 'steam.sh')
    env = steam.steam_environment()
    monkeypatch.setenv('RECOMP_LAUNCHER_SCRIPT', '1:quit')
    env = steam.steam_environment()
    assert 'APPIMAGE' not in env and 'APPDIR' not in env and env.get('HOME') == steam.os.environ.get('HOME')
    assert not [k for k in env if k.startswith(('DEFJAM_', 'RECOMP_'))]


def test_uninstall_removes_the_shortcut_the_app_added():
    # A shortcut runs the AppImage file (launch_target). program() is inside its temporary mount on
    # Linux, so Uninstall matched nothing: it deleted the artwork and left the shortcut in the library.
    text = (ROOT / 'src/app/app_steam.cpp').read_text(encoding='utf-8')
    assert text.count('"--app-program", launch_target().string()') == 2
    assert '"--app-program", program()' not in text


def test_removing_with_the_shortcuts_own_path_takes_it_and_its_artwork(tmp_path):
    grid = tmp_path / 'art'
    grid.mkdir()
    (grid / 'wide.png').write_bytes(b'wide')
    root = tmp_path / 'steam'
    config = root / 'userdata/12345/config'
    config.mkdir(parents=True)
    (config / 'shortcuts.vdf').write_bytes(b'\x00shortcuts\x00\x08\x08')
    record = tmp_path / 'steam.json'
    appimage = tmp_path / 'DefJamRecompiled.AppImage'
    steam.add(root, appimage, '', [grid], record, running=lambda _: [])
    steam.remove(root, appimage, [grid], record, running=lambda _: [])
    bodies, _ = steam.read_shortcuts(config / 'shortcuts.vdf')
    assert bodies == []
    assert not list((config / 'grid').glob('*.png'))
