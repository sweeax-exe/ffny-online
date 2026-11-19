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

    def compose(data_, game, packs, root=None, log=None, reserved=(), portraits=True):
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
    assert 'out[i].off = out[i].local ? pc_content_own_is_off(out[i].id)' in content
    start = netplay.split('static int start(const char *server')[1].split('\n}\n')[0]
    assert 'pc_content_local_offline(names, sizeof names)' in start and 'PC_CONTENT_LOCAL_LINE' in start
    assert 'they work offline only. Turn them off in the launcher (Content) to play online.' in header


# -- a player's own new fighter whose place a community fighter takes (jackboy66's case, 2026-11-15) ------------------

def fighter_folder(root, name, fe_id=None):
    """A folder whose install.json has a new-fighter op (settle_places reads only the op)."""
    folder = Path(root) / name
    folder.mkdir(parents=True)
    op = {'op': 'new-fighter', 'template_fe_id': 55, 'template_model': 'V2IP_109A', 'model': 'V2IP_7AAA',
          'name': 'X'}
    if fe_id is not None:
        op['fe_id'] = fe_id
    (folder / 'install.json').write_text(json.dumps(install_json('new-fighter', ops=[op])), encoding='utf-8')
    return folder


def test_settle_places_gives_own_fighters_own_numbers(tmp_path):
    """Build 98: a player's own new fighter plays by an own number (unlimited.own_number), never a place: community
    places and numbers stay as they are, an own fighter keeps the number it has, a place of an older Studio becomes
    a number, and no two fighters ever share a feID."""
    cc = mods.content_compose
    unl = cc.unlimited
    community = [dict(id='studio-%d' % fe, folder=fighter_folder(tmp_path, 'c%d' % fe), fighter_fe_id=fe)
                 for fe in (101, 112, 115, 1000, 1001)]
    venue = dict(id='a-venue', folder=own_pack(tmp_path, 'v', install=install_json()), fighter_fe_id=0)
    jimmy = dict(id='my-studio-jimmy', folder=fighter_folder(tmp_path, 'j'), fighter_fe_id=100, local=True)
    kept = unl.own_number('my-studio-naruto') + 1 if unl.own_number('my-studio-naruto') < unl.OWN_LAST else unl.OWN_FIRST
    naruto = dict(id='my-studio-naruto', folder=fighter_folder(tmp_path, 'n'), fighter_fe_id=kept, local=True)
    scotty = dict(id='my-studio-scotty', folder=fighter_folder(tmp_path, 's', fe_id=127), local=True)
    lines = []
    packs = community + [venue, jimmy, naruto, scotty]
    moves = cc.settle_places(packs, reserved={101, 112, 115, 1000, 1001, 1002}, log=lines.append)
    assert [p['fighter_fe_id'] for p in community] == [101, 112, 115, 1000, 1001]
    assert naruto['fighter_fe_id'] == kept                          # its number stays its own
    assert jimmy['fighter_fe_id'] == unl.own_number('my-studio-jimmy', {kept})
    assert scotty['fighter_fe_id'] == unl.own_number('my-studio-scotty', {kept, jimmy['fighter_fe_id']})
    assert all(unl.is_own_number(p['fighter_fe_id']) for p in (jimmy, naruto, scotty))
    assert [m[0] for m in moves] == ['my-studio-jimmy', 'my-studio-scotty'] and moves[1][1] == 127
    assert venue['fighter_fe_id'] == 0 and not any('place_problem' in p for p in packs)
    assert any('my-studio-jimmy: own new fighter number' in line and 'it had place 100' in line for line in lines)
    ids = [p['fighter_fe_id'] for p in packs if p is not venue]
    assert len(ids) == len(set(ids))
    # every community place taken (the 48 and beyond): own fighters still go in, all of them
    full = [dict(id='c-%d' % fe, folder=community[0]['folder'], fighter_fe_id=fe) for fe in cc.newfighter.PLACE_ORDER]
    mine = [dict(id='my-f%02d' % i, folder=fighter_folder(tmp_path, 'f%02d' % i), local=True) for i in range(60)]
    cc.settle_places(full + mine, log=lambda t: None)
    assert not any('place_problem' in p for p in full + mine)
    assert len({p['fighter_fe_id'] for p in mine}) == 60 and all(unl.is_own_number(p['fighter_fe_id']) for p in mine)
    # a second community pack on a place given already (the server never does it) is left out
    twice = dict(id='c-twice', folder=community[0]['folder'], fighter_fe_id=101)
    cc.settle_places([community[0], twice], log=lambda t: None)
    assert 'place_problem' in twice and 'place_problem' not in community[0]


def test_own_numbers_are_stable_and_never_clash(tmp_path):
    cc = mods.content_compose
    unl = cc.unlimited
    assert unl.OWN_FIRST > 1000 and unl.OWN_LAST < cc.newfighter.MORE_ID and len(str(unl.OWN_LAST)) == 5
    ids = ['my-studio-fighter-%d' % i for i in range(500)]
    first = [unl.own_number(i) for i in ids]
    assert first == [unl.own_number(i) for i in ids]                # the same on every launch and PC
    assert all(unl.is_own_number(n) for n in first)
    # in any order and with community numbers around, the compose gives each pack the same number as long as nothing
    # holds it: settle_places in folder order, then once more with the stored numbers (the next launch)
    packs = [dict(id=i, folder=fighter_folder(tmp_path, 'p%03d' % k), local=True) for k, i in enumerate(ids[:60])]
    community = [dict(id='c%d' % n, folder=packs[0]['folder'], fighter_fe_id=n) for n in range(1000, 1200)]
    cc.settle_places(community + packs, reserved=range(1000, 1300), log=lambda t: None)
    got = {p['id']: p['fighter_fe_id'] for p in packs}
    assert len(set(got.values())) == 60 and not set(got.values()) & set(range(1000, 1300))
    again = [dict(p, fighter_fe_id=got[p['id']]) for p in reversed(packs)]
    assert cc.settle_places(community + again, log=lambda t: None) == []
    assert {p['id']: p['fighter_fe_id'] for p in again} == got
    # a community number that happens to be an own pack's (the service never gives one): the own pack moves
    holder = dict(id='c-own', folder=packs[0]['folder'], fighter_fe_id=got[ids[0]])
    moved = [dict(p, fighter_fe_id=got[p['id']]) for p in packs]
    cc.settle_places([holder] + moved, log=lambda t: None)
    assert moved[0]['fighter_fe_id'] != got[ids[0]] and unl.is_own_number(moved[0]['fighter_fe_id'])
    assert [p['fighter_fe_id'] for p in moved[1:]] == [got[i] for i in ids[1:60]]
    # more than OWN_MAX on: the rest are left out with the reason, never silently
    many = [dict(id='my-m%03d' % i, folder=packs[0]['folder'], local=True) for i in range(unl.OWN_MAX + 3)]
    cc.settle_places(many, log=lambda t: None)
    out = [p for p in many if 'place_problem' in p]
    assert [p['id'] for p in out] == [p['id'] for p in many[unl.OWN_MAX:]]
    assert 'more than %d of your own new fighters are on' % unl.OWN_MAX in out[0]['place_problem']


def test_compose_gives_an_own_fighter_its_number_and_keeps_it(tmp_path, monkeypatch):
    """The launcher's compose: the own number goes into the pack's local.json and content.ini, and the line into the
    progress; a pack the composition leaves out has not_in_game=<reason> in content.ini."""
    data = tmp_path / 'data'
    folder = own_pack(data, 'my-studio-naruto', kind='content', type_='new-fighter',
                      install=install_json('new-fighter', ops=[{'op': 'new-fighter', 'template_fe_id': 55,
                                                               'template_model': 'V2IP_109A', 'model': 'V2IP_7AAA',
                                                               'name': 'Naruto'}]),
                      extra={'fighter_fe_id': 117})
    manager = mods.Manager(data)
    seen = {}

    def compose(d, game, packs, root=None, log=print, reserved=(), portraits=True):
        mods.content_compose.settle_places(packs, reserved, log)
        seen['packs'] = packs
        state = {'skipped': [['my-studio-naruto', 'the model V2IP_7AAA is\nanother fighter\'s already']]}
        (Path(root) / mods.content_compose.STATE).write_text(json.dumps(state), encoding='utf-8')
        return {'applied': [], 'skipped': [tuple(s) for s in state['skipped']], 'files': [], 'unchanged': False}
    monkeypatch.setattr(mods.content_compose, 'compose', compose)
    monkeypatch.setattr(manager, 'catalogue_places', lambda: {117})
    said = []
    monkeypatch.setattr(manager, 'progress', said.append)
    manager.compose()
    number = mods.content_compose.unlimited.own_number('my-studio-naruto')
    assert seen['packs'][0]['fighter_fe_id'] == number
    assert json.loads((folder / 'local.json').read_text(encoding='utf-8'))['fighter_fe_id'] == number
    ini = (data / 'mod-manager' / 'content.ini').read_text(encoding='utf-8')
    assert 'fighter_fe_id=%d' % number in ini
    assert "not_in_game=the model V2IP_7AAA is another fighter's already" in ini
    assert manager.content_reasons() == {'my-studio-naruto': "the model V2IP_7AAA is another fighter's already"}
    assert any('own new fighter number %d' % number in s for s in said)


def test_compose_also_builds_the_own_packs_alone_for_offline_play_without_community_content(tmp_path, monkeypatch):
    """Build 90: [content] community_offline=0 serves <data>/mod-manager/own-packs/content-files (only the player's
    own packs), built by the same compose run that builds content-files."""
    data = tmp_path / 'data'
    own_pack(data, 'my-on', install=install_json(entries=[dict(ENTRY, entry='V2BG_03A.xsh', base_entry='V2BG_03A.xsh')]))
    monkeypatch.delenv('RECOMP_SETTINGS', raising=False)
    calls = []

    def compose(data_, game, packs, root=None, log=None, reserved=(), portraits=True):
        calls.append((Path(root).name, [p['id'] for p in packs], portraits))
        return {'applied': [], 'skipped': [], 'files': [], 'unchanged': True}
    monkeypatch.setattr(mods.content_compose, 'compose', compose)
    manager = mods.Manager(data)
    monkeypatch.setattr(manager, 'composed_packs', lambda: [dict(id='community-fighter', version='1', content_sha256='x',
                                                                  kind='content', order=1, folder=data)])
    manager.compose()
    # the own-packs pass leaves <data>/online-art alone (builds 90-91: its empty set removed every community
    # fighter's hub portrait and skin picture the full pass had just drawn)
    assert calls == [('mod-manager', ['community-fighter', 'my-on'], True), ('own-packs', ['my-on'], False)]


def test_compose_leaves_out_the_own_packs_in_own_off_txt(tmp_path, monkeypatch):
    """Build 94: the own packs that are off are listed in <data>/mod-manager/own-off.txt (a setting holds 1023
    characters, about 60 ids): with 300 own packs the compose step leaves out exactly those, in both passes."""
    data = tmp_path / 'data'
    ids = ['my-roster-fighter-%03d' % i for i in range(300)]
    for pack in ids:
        own_pack(data, pack, install=install_json(entries=[dict(ENTRY, entry='V2BG_03A.xsh', base_entry='V2BG_03A.xsh')]))
    off = ids[::2]
    (data / 'mod-manager').mkdir(exist_ok=True)
    (data / 'mod-manager' / 'own-off.txt').write_text('\n'.join(off) + '\n', encoding='utf-8')
    monkeypatch.delenv('RECOMP_SETTINGS', raising=False)
    calls = []

    def compose(data_, game, packs, root=None, log=None, reserved=(), portraits=True):
        calls.append((Path(root).name, sorted(p['id'] for p in packs)))
        return {'applied': [], 'skipped': [], 'files': [], 'unchanged': True}
    monkeypatch.setattr(mods.content_compose, 'compose', compose)
    manager = mods.Manager(data)
    monkeypatch.setattr(manager, 'composed_packs', lambda: [])
    manager.compose()
    on = sorted(set(ids) - set(off))
    assert calls == [('mod-manager', on), ('own-packs', on)]
    assert len(on) == 150
    # every one back on: everything is composed
    (data / 'mod-manager' / 'own-off.txt').write_text('', encoding='utf-8')
    calls.clear()
    manager.compose()
    assert calls == [('mod-manager', sorted(ids)), ('own-packs', sorted(ids))]
