"""Add to Steam for the app (setup/steam_shortcuts.py, design §7): the shortcut written into a stand-in
Steam folder's shortcuts.vdf, the artwork, and Steam closed around the change. Synthetic data only;
nothing here reads or writes a real Steam."""
import importlib.util
import json
from pathlib import Path
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
    assert ours['LaunchOptions'] == '--play --install "%s"' % INSTALL
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
