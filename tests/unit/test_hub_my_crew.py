"""MY CREW in the online hub (build 77): an entry after TURF WAR that opens the chat window's CREW tab on
the crew's page in one step (src/hooks/online_screen.cpp draw_hub, src/hooks/social_ui.inc open_my_crew)."""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCREEN = (ROOT / 'src/hooks/online_screen.cpp').read_text(encoding='utf-8')
SOCIAL_UI = (ROOT / 'src/hooks/social_ui.inc').read_text(encoding='utf-8')


def hub_consts():
    m = re.search(r'const int HUB_ITEMS = (\d+),([^;]+);', SCREEN)
    assert m
    consts = {'HUB_ITEMS': int(m.group(1))}
    for name, value in re.findall(r'(HUB_\w+) = (\d+)', m.group(2)):
        consts[name] = int(value)
    return consts


def test_order_puts_my_crew_after_turf_war():
    c = hub_consts()
    m = re.search(r'const int HUB_ORDER\[HUB_ITEMS\] = \{([^}]*)\}', SCREEN)
    order = [x.strip() for x in m.group(1).split(',')]
    assert len(order) == c['HUB_ITEMS']
    assert order[order.index('HUB_TURF') + 1] == 'HUB_CREW'
    # with TURF WAR hidden it follows MY STATS (item 4)
    assert order[order.index('HUB_TURF') - 1] == '4'
    resolved = sorted(c[x] if x in c else int(x) for x in order)
    assert resolved == list(range(c['HUB_ITEMS']))


def test_labels_line_up_with_the_index():
    c = hub_consts()
    body = SCREEN.split('void draw_hub(', 1)[1].split('std::vector<int> quick_modes', 1)[0]
    items = re.search(r'items\[\] = \{(.*?)\};', body, re.S).group(1)
    subs = re.search(r'subs\[\] = \{(.*?)\};', body, re.S).group(1)
    items = re.findall(r'"([^"]*)"', items)
    subs = re.findall(r'"([^"]*)"', subs)
    assert len(items) == len(subs) == c['HUB_ITEMS']
    assert items[c['HUB_CREW']] == 'MY CREW'
    assert subs[c['HUB_CREW']] == "YOUR CREW'S PAGE"
    descs = re.search(r'descs\[\] = \{(.*?)\n    \};', body, re.S).group(1)
    # adjacent literals concatenate: one description ends with ".",
    assert descs.count('.",') == c['HUB_ITEMS']


def test_entry_is_gated_and_opens_the_crew_page():
    assert 'crew = my_crew_shown()' in SCREEN and '(i != HUB_CREW || crew)' in SCREEN
    case = SCREEN.split('case HUB_CREW:', 1)[1].split('case HUB_LEAVE:', 1)[0]
    assert '!signed_in' in case and 'Sign in with Discord' in case and 'open_my_crew()' in case
    shown = SOCIAL_UI.split('bool my_crew_shown()', 1)[1].split('\n}\n', 1)[0]
    assert 'social_enabled()' in shown and 'C.v.crews' in shown and 'C.v.crew' in shown
    assert 'RECOMP_SOCIAL' in shown        # switched off for the copy: never shown
    opener = SOCIAL_UI.split('void open_my_crew()', 1)[1].split('\n}\n', 1)[0]
    for part in ('C.open = ', 'C.tab = TAB_CREW', 'C.crew_view = 0', 'C.opened_at = GetTickCount64()'):
        assert part in opener, part


def test_gallery_has_the_hub_with_my_crew():
    pages = SCREEN.split('void gallery_step()', 1)[1].split('};', 1)[0]
    assert '"hub-crew"' in pages and '"hub-crew-signin"' in pages
    assert 'if (gallery_on()) return G.my_crew;' in SOCIAL_UI
