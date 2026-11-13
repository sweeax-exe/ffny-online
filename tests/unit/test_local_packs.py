"""The player's own packs (FFNY Studio's "Use in my game"): <data>/mods/<id>/local.json, composed after the community
packs, switchable, listed in content.ini with local=1 and online_ok (docs/content-packs.md)."""
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import test_mod_manager as base  # noqa: E402  (the loaded mods module)
sys.path.pop(0)
mods = base.mods

ENTRY = dict(archive='assets/v2bg.viv', base_archive='assets/v2bg.viv', mode='replace', delta='deltas/a.ffdelta',
             original_sha256='0' * 64, result_sha256='1' * 64, result_size=4)


def own_pack(data, name, kind='replacement', type_='venue', install=None, extra=None):
    folder = Path(data) / 'mods' / name
    folder.mkdir(parents=True)
    info = dict(schema=1, source='ffny-studio', name=name.upper(), kind=kind, type=type_, version='1.2.0')
    info.update(extra or {})
    (folder / 'local.json').write_text(json.dumps(info), encoding='utf-8')
    if install is None:
        (folder / 'manifest.ini').write_text('[pack]\nname=x\n', encoding='utf-8')
    else:
        (folder / 'install.json').write_text(json.dumps(install), encoding='utf-8')
        (folder / 'deltas').mkdir()
        (folder / 'deltas' / 'a.ffdelta').write_bytes(b'FFDL')
    return folder


def install_json(type_='venue', entries=(), ops=()):
    return dict(schema=1, type=type_, entries=list(entries), ops=list(ops))


def test_online_safe_only_for_textures(tmp_path):
    tex = own_pack(tmp_path, 'my-tex')
    xsh = own_pack(tmp_path, 'my-xsh', install=install_json(entries=[dict(ENTRY, entry='V2BG_03A.xsh',
                                                                            base_entry='V2BG_03A.xsh')]))
    model = own_pack(tmp_path, 'my-model', install=install_json(entries=[dict(ENTRY, entry='V2BG_03A.o',
                                                                                base_entry='V2BG_03A.o')]))
    cli = own_pack(tmp_path, 'my-cli', install=install_json(entries=[dict(ENTRY, entry='V2BG_03A.cli',
                                                                            base_entry='V2BG_03A.cli')]))
    added = own_pack(tmp_path, 'my-add', install=install_json(entries=[dict(ENTRY, entry='V2BG_28A.xsh',
                                                                              base_entry='V2BG_03A.xsh',
                                                                              mode='add')]))
    clothing = own_pack(tmp_path, 'my-cloth', kind='content', type_='clothing',
                        install=install_json('clothing', ops=[{'op': 'colour'}]))
    broken = own_pack(tmp_path, 'my-broken', install={'schema': 9})
    assert mods.local_online_safe(tex) and mods.local_online_safe(xsh)
    for folder in (model, cli, added, clothing, broken):
        assert not mods.local_online_safe(folder), folder.name


def test_local_packs_listed_and_written(tmp_path):
    data = tmp_path / 'data'
    own_pack(data, 'my-tex')
    own_pack(data, 'my-cloth', kind='content', type_='clothing', install=install_json('clothing', ops=[{'op': 'colour'}]),
             extra={'fighter_fe_id': True})
    (data / 'mods' / 'hand-made').mkdir()                    # a folder without local.json: not one of these
    (data / 'mods' / 'Bad_Name').mkdir()
    (data / 'mods' / 'Bad_Name' / 'local.json').write_text('{}', encoding='utf-8')
    m = mods.Manager(data)
    packs = {p['id']: p for p in m.local_packs()}
    assert sorted(packs) == ['my-cloth', 'my-tex']
    assert packs['my-tex']['online_ok'] and not packs['my-tex']['composed']
    assert not packs['my-cloth']['online_ok'] and packs['my-cloth']['composed']
    assert packs['my-cloth']['fighter_fe_id'] == 0          # a bool is not a place
    assert all(p['order'] >= mods.LOCAL_ORDER for p in packs.values())
    m.write_content(0)
    text = (data / 'mod-manager' / 'content.ini').read_text(encoding='utf-8')
    cloth = text.split('[pack.my-cloth]')[1].split('[pack.')[0]
    assert 'local=1' in cloth and 'online_ok=0' in cloth and 'composed=1' in cloth and 'kind=content' in cloth
    tex = text.split('[pack.my-tex]')[1].split('[pack.')[0]
    assert 'online_ok=1' in tex and 'composed=1' not in tex


def test_content_hash_follows_the_files(tmp_path):
    folder = own_pack(tmp_path, 'my-tex')
    m = mods.Manager(tmp_path)
    first = m.local_packs()[0]['content_sha256']
    (folder / 'manifest.ini').write_text('[pack]\nname=y\n', encoding='utf-8')
    assert m.local_packs()[0]['content_sha256'] != first


def test_compose_takes_own_packs_that_are_on(tmp_path, monkeypatch):
    data = tmp_path / 'data'
    own_pack(data, 'my-on', install=install_json(entries=[dict(ENTRY, entry='V2BG_03A.xsh', base_entry='V2BG_03A.xsh')]))
    own_pack(data, 'my-off', install=install_json(entries=[dict(ENTRY, entry='V2BG_03A.xsh', base_entry='V2BG_03A.xsh')]))
    own_pack(data, 'my-tex')                                  # textures only: nothing to compose
    (data / 'settings.ini').write_text('[content]\noff=my-off\n', encoding='utf-8')
    monkeypatch.delenv('RECOMP_SETTINGS', raising=False)
    seen = {}

    def compose(data_, game, packs, root=None, log=None):
        seen['ids'] = [p['id'] for p in packs]
        return {'applied': [], 'skipped': [], 'files': [], 'unchanged': True}
    monkeypatch.setattr(mods.content_compose, 'compose', compose)
    mods.Manager(data).compose()
    assert seen['ids'] == ['my-on']


def test_game_keeps_own_packs_offline():
    """pc_content.c leaves local packs out of the online print and netplay.c refuses online while one that is not
    online-safe is on, with the one plain line."""
    hooks = Path(__file__).resolve().parents[2] / 'src' / 'hooks'
    content = (hooks / 'pc_content.c').read_text(encoding='utf-8')
    netplay = (hooks / 'netplay.c').read_text(encoding='utf-8')
    header = (hooks / 'pc_content.h').read_text(encoding='utf-8')
    assert 'if (packs[i].local) continue;' in content.split('uint32_t pc_content_print(void)')[1].split('\n}\n')[0]
    assert 'out[i].off = (out[i].local ||' in content
    start = netplay.split('static int start(const char *server')[1].split('\n}\n')[0]
    assert 'pc_content_local_offline(names, sizeof names)' in start and 'PC_CONTENT_LOCAL_LINE' in start
    assert 'they work offline only. Turn them off in the launcher (Content) to play online.' in header
