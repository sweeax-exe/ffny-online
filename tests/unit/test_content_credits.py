"""Build 94: the creator's credits on community packs. They come from the signed catalogue (index-v6.json: the mod
key "credits"), reach content.ini at every sync (the Content page), follow a credits edit of the account service without
installing the pack again, and are never part of a pack's content hash, its signature or the online content print."""
import configparser
import json
from pathlib import Path
import sys
from unittest.mock import patch

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import test_content_sync as sync  # noqa: E402  (the signed catalogue fixtures)
sys.path.pop(0)
mods = sync.mods
setup = sync.setup      # the fixture
ROOT = Path(__file__).resolve().parents[2]


def section(manager, name):
    c = configparser.ConfigParser(interpolation=None)
    c.read(manager.root / 'content.ini', encoding='utf-8')
    return c['pack.' + name]


def test_contract_takes_credits_and_refuses_bad_ones(setup, tmp_path):
    manager, key = setup
    path, mod = sync.base.package(tmp_path)
    sync.ffny(key, path, mod)
    for good in ('Texture by Free (Club Murder Def Jam)', 'Texture by Free\nVoice by Ana', 'x' * 300,
                 '\n'.join('c%d' % i for i in range(6)), 'Modell von Jürgen'):
        mods.contract.validate_mod(dict(mod, credits=good))
    for bad in ('', '   ', 'x' * 301, '\n'.join('c%d' % i for i in range(7)), 'bell\x07', 'tab\there', 'del\x7f', 5, None,
                ['Texture by Free']):
        with pytest.raises(ValueError):
            mods.contract.validate_mod(dict(mod, credits=bad))
    with pytest.raises(ValueError):          # still strict about every other key
        mods.contract.validate_mod(dict(mod, credit='Texture by Free'))


def test_a_catalogue_with_credits_verifies_and_builds_byte_for_byte(setup, tmp_path):
    manager, key = setup
    path, mod = sync.base.package(tmp_path)
    sync.ffny(key, path, mod)
    mod['credits'] = 'Texture by Free\nVoice by Ana'
    raw, sig = sync.publish(key, [mod], 1)
    index = mods.verify_index(raw, sig, manager.keys)
    assert index['mods'][0]['credits'] == 'Texture by Free\nVoice by Ana'


def test_sync_writes_credits_into_content_ini_and_follows_an_edit_without_installing(setup, tmp_path):
    manager, key = setup
    a_path, a = sync.base.package(tmp_path, name='zeta-skin')
    b_path, b = sync.base.package(tmp_path, name='alpha-hat', color='blue')
    sync.ffny(key, a_path, a, order=1)
    sync.ffny(key, b_path, b, kind='content', order=2, type_='clothing')
    a['credits'] = 'Texture by Free (Club Murder Def Jam)\nVoice by Ana'
    archives = {a['versions'][0]['archive']['sha256']: a_path, b['versions'][0]['archive']['sha256']: b_path}
    raw, sig = sync.publish(key, [a, b], 1)
    sync.run_sync(manager, raw, sig, archives)
    assert section(manager, 'zeta-skin')['credits'] == 'Texture by Free (Club Murder Def Jam) | Voice by Ana'
    assert section(manager, 'alpha-hat')['credits'] == ''           # no credits: the key is there, empty
    hash_before = section(manager, 'zeta-skin')['content_sha256']
    installed_before = json.dumps(manager.state()['installed']['zeta-skin'], sort_keys=True)
    # the team edits the credits: a new revision, the same version and archive; no download, the same receipt
    a['credits'] = 'Texture by Free (Club Murder Def Jam)\nVoice by Ana\nModel by Juan'
    raw2, sig2 = sync.publish(key, [a, b], 2)
    sync.run_sync(manager, raw2, sig2, {})
    assert 'up to date: 2 pack(s)' in manager.message
    assert section(manager, 'zeta-skin')['credits'] == \
        'Texture by Free (Club Murder Def Jam) | Voice by Ana | Model by Juan'
    assert section(manager, 'zeta-skin')['content_sha256'] == hash_before       # the pack itself is untouched
    assert json.dumps(manager.state()['installed']['zeta-skin'], sort_keys=True) == installed_before
    # removed again: gone from the Content page
    del a['credits']
    raw3, sig3 = sync.publish(key, [a, b], 3)
    sync.run_sync(manager, raw3, sig3, {})
    assert section(manager, 'zeta-skin')['credits'] == ''


def test_credits_do_not_reach_the_news_check(setup, tmp_path):
    """A credits edit is a new catalogue revision but no new content: nothing to restart for."""
    manager, key = setup
    a_path, a = sync.base.package(tmp_path, name='first')
    sync.ffny(key, a_path, a, order=1)
    raw, sig = sync.publish(key, [a], 1)
    sync.run_sync(manager, raw, sig, {a['versions'][0]['archive']['sha256']: a_path})
    a['credits'] = 'Texture by Free'
    raw2, sig2 = sync.publish(key, [a], 2)

    def fetch(url, limit):
        return sig2 if url.endswith('.sig') else raw2
    with patch.object(mods, 'fetch_small', fetch):
        assert manager.check_news('https://play.defjam.online/updates/content/index-v6.json') == []


def test_credits_line_is_one_line():
    assert mods.credits_line('A\nB\n\n  C   D ') == 'A | B | C D'
    assert mods.credits_line('') == '' and mods.credits_line(None) == ''
    assert len(mods.credits_line('x' * 300 + '\n' + 'y' * 300)) == 500


def test_the_content_print_never_reads_credits():
    """pc_content_print hashes id, version and content_sha256 of the always-on packs (and the skin marker); the
    credits are another field of the same section and never part of online matching."""
    source = (ROOT / 'src/hooks/pc_content.c').read_text(encoding='utf-8')
    body = source[source.index('uint32_t pc_content_print(void)'):source.index('/* ---- build 85: skins ---- */')]
    assert 'credits' not in body
    assert 'parts[0] = packs[i].id; parts[1] = packs[i].version; parts[2] = packs[i].content_sha256;' in body
    assert 'CONTENT_TEXT(credits)' in source
    header = (ROOT / 'src/hooks/pc_content.h').read_text(encoding='utf-8')
    assert 'char credits[512];' in header


def test_the_launcher_shows_credits_on_the_content_and_community_pages():
    content = (ROOT / 'src/hooks/pc_launcher_content.inc').read_text(encoding='utf-8')
    assert content.count('credits_draw(ld, p.credits') == 2       # a single pack's card and a pack of several mods
    assert 'credits_height(p.credits' in content
    community = (ROOT / 'src/hooks/pc_launcher_community.inc').read_text(encoding='utf-8')
    assert 'c.credits = j["credits"].str();' in community and '"Credits: " + c.credits' in community
