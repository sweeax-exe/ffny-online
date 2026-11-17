"""Build 87, the notice of content to come ("NEW CONTENT AT 17:00 - 3 new fighters, 1 skin"): the rules and texts
(src/hooks/content_release_logic.c, built natively with MSVC; skipped without it) and the wiring in the game: the
schedule is asked with build 86's catalogue check, the notice sits where the UPDATE NOW notice sits, and the online
screens' gallery has its page."""
import re
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
HOOKS = ROOT / 'src/hooks'

DRIVER = r'''
#include <stdio.h>
#include <string.h>
#include "content_release_logic.h"
static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
#define TEXT(call, want) do { char _b[256]; call; if (strcmp(_b, want)) { \
    printf("FAIL line %d: got \"%s\", want \"%s\"\n", __LINE__, _b, want); fails++; } } while (0)
static CrInfo info(int count, int nf, int skin, int map, int clothing, int look, int lang, int pack, int update)
{
    CrInfo i; memset(&i, 0, sizeof i);
    i.server_time = 1000; i.local_time = 500000; i.release_at = 1000 + 8100; i.count = count;
    i.kinds[CR_NEW_FIGHTER] = nf; i.kinds[CR_SKIN] = skin; i.kinds[CR_MAP] = map; i.kinds[CR_CLOTHING] = clothing;
    i.kinds[CR_LOOK] = look; i.kinds[CR_LANGUAGE] = lang; i.kinds[CR_PACK] = pack; i.kinds[CR_UPDATE] = update;
    return i;
}
int main(void)
{
    CrInfo i, e;
    CrNotice n;
    unsigned long long ms = 100000;

    /* the service's answer (server: relay/accounts/packs_release.go NextRelease) */
    CHECK(cr_parse("{\"release_at\":1763312400,\"count\":4,\"items\":[{\"title\":\"Doc Nine\",\"kind\":\"new fighter\"},"
                   "{\"title\":\"Kimora 2\",\"kind\":\"new fighter\"},{\"title\":\"Red Hoodie Crow\",\"kind\":\"skin\"},"
                   "{\"title\":\"The Block\",\"kind\":\"new fighter\"}],\"next_window\":1763312400,"
                   "\"server_time\":1763304300}", 5000, &i));
    CHECK(i.release_at == 1763312400 && i.server_time == 1763304300 && i.count == 4 && i.local_time == 5000);
    CHECK(i.kinds[CR_NEW_FIGHTER] == 3 && i.kinds[CR_SKIN] == 1 && i.kinds[CR_MAP] == 0);
    /* the PC's clock and zone do not matter: 8100 s after the answer came */
    CHECK(cr_local_release(&i) == 5000 + 8100 && cr_remaining(&i, 5000) == 8100 && cr_remaining(&i, 6000) == 7100);
    CHECK(cr_shows(&i, 5000) && cr_shows(&i, 13099) && !cr_shows(&i, 13100) && !cr_shows(&i, 20000));
    /* nothing scheduled: an answer, and nothing shows */
    CHECK(cr_parse("{\"release_at\":0,\"count\":0,\"items\":[],\"next_window\":1763312400,\"server_time\":1763304300}",
                   5000, &e));
    CHECK(e.count == 0 && !cr_shows(&e, 5000));
    /* a title is a value, whatever it says: names in it are not read, nor a quote, nor a kind this build has no
     * word for (the count still has it) */
    CHECK(cr_parse("{\"release_at\":9100,\"count\":3,\"items\":[{\"title\":\"\\\"kind\\\":\\\"skin\\\" count\",\"kind\":\"map\"},"
                   "{\"title\":\"server_time\",\"kind\":\"hologram\"},{\"title\":\"kind\",\"kind\":\"update\"}],"
                   "\"next_window\":0,\"server_time\":1000}", 0, &i));
    CHECK(i.count == 3 && i.kinds[CR_MAP] == 1 && i.kinds[CR_SKIN] == 0 && i.kinds[CR_UPDATE] == 1 && i.server_time == 1000);
    TEXT(cr_kinds_text(&i, _b, sizeof _b), "1 map, 1 update, 1 more");
    /* not the service's answer: a page of an error, a cut-off answer, no clock */
    CHECK(!cr_parse("<html>502 Bad Gateway</html>", 0, &i));
    CHECK(!cr_parse("{\"release_at\":9100,\"count\":3,\"items\":[{\"title\":\"Doc N", 0, &i));
    CHECK(!cr_parse("{\"release_at\":9100,\"count\":3,\"items\":[]}", 0, &i));
    CHECK(!cr_parse("{\"release_at\":9100,\"count\":-1,\"server_time\":1000}", 0, &i));
    CHECK(!cr_parse("", 0, &i) && !cr_parse(NULL, 0, &i));

    /* what is coming */
    i = info(4, 3, 1, 0, 0, 0, 0, 0, 0);
    TEXT(cr_kinds_text(&i, _b, sizeof _b), "3 new fighters, 1 skin");
    i = info(1, 1, 0, 0, 0, 0, 0, 0, 0);
    TEXT(cr_kinds_text(&i, _b, sizeof _b), "1 new fighter");
    i = info(7, 0, 2, 1, 1, 1, 1, 0, 1);            /* more kinds than parts: the first three, then the rest */
    TEXT(cr_kinds_text(&i, _b, sizeof _b), "2 skins, 1 map, 1 clothing item, 3 more");
    i = info(8, 1, 0, 2, 3, 2, 0, 0, 0);            /* four kinds fit */
    TEXT(cr_kinds_text(&i, _b, sizeof _b), "1 new fighter, 2 maps, 3 clothing items, 2 fighter looks");
    i = info(6, 0, 0, 0, 0, 0, 2, 2, 2);
    TEXT(cr_kinds_text(&i, _b, sizeof _b), "2 languages, 2 packs, 2 updates");
    i = info(25, 12, 8, 0, 0, 0, 0, 0, 0);          /* the answer names 20 of 25 */
    TEXT(cr_kinds_text(&i, _b, sizeof _b), "12 new fighters, 8 skins, 5 more");
    i = info(26, 10, 5, 3, 2, 0, 0, 0, 0);          /* four kinds and unnamed ones: the fourth joins the rest */
    TEXT(cr_kinds_text(&i, _b, sizeof _b), "10 new fighters, 5 skins, 3 maps, 8 more");
    i = info(2, 0, 0, 0, 0, 0, 0, 0, 0);            /* only kinds this build has no word for */
    TEXT(cr_kinds_text(&i, _b, sizeof _b), "2 new packs");
    i = info(1, 0, 0, 0, 0, 0, 0, 0, 0);
    TEXT(cr_kinds_text(&i, _b, sizeof _b), "1 new pack");
    { char small[12]; i = info(4, 3, 1, 0, 0, 0, 0, 0, 0);     /* a small buffer is cut, never overrun */
      cr_kinds_text(&i, small, sizeof small); CHECK(strlen(small) == 11 && !strncmp(small, "3 new fight", 11)); }

    /* how long until then */
    TEXT(cr_in_text(8100, _b, sizeof _b), "in 2h 15m");
    TEXT(cr_in_text(7200, _b, sizeof _b), "in 2h");
    TEXT(cr_in_text(7201, _b, sizeof _b), "in 2h 1m");
    TEXT(cr_in_text(3600, _b, sizeof _b), "in 1h");
    TEXT(cr_in_text(3599, _b, sizeof _b), "in 1h");          /* 59 min 59 s rounds up, never "in 60 min" */
    TEXT(cr_in_text(3540, _b, sizeof _b), "in 59 min");
    TEXT(cr_in_text(900, _b, sizeof _b), "in 15 min");
    TEXT(cr_in_text(61, _b, sizeof _b), "in 2 min");
    TEXT(cr_in_text(60, _b, sizeof _b), "in 1 min");
    TEXT(cr_in_text(59, _b, sizeof _b), "in under a minute");
    TEXT(cr_in_text(0, _b, sizeof _b), "in under a minute");
    TEXT(cr_in_text(86400, _b, sizeof _b), "in 1d");
    TEXT(cr_in_text(86400 + 3 * 3600 + 60, _b, sizeof _b), "in 1d 3h");

    /* the time on the PC's clock: 24-hour, or AM/PM where the PC shows that */
    TEXT(cr_clock_text(17, 0, 0, _b, sizeof _b), "17:00");
    TEXT(cr_clock_text(9, 5, 0, _b, sizeof _b), "09:05");
    TEXT(cr_clock_text(0, 30, 0, _b, sizeof _b), "00:30");
    TEXT(cr_clock_text(17, 0, 1, _b, sizeof _b), "5:00 PM");
    TEXT(cr_clock_text(0, 30, 1, _b, sizeof _b), "12:30 AM");
    TEXT(cr_clock_text(12, 0, 1, _b, sizeof _b), "12:00 PM");
    TEXT(cr_clock_text(11, 59, 1, _b, sizeof _b), "11:59 AM");
    TEXT(cr_headline(0, 17, 0, 0, _b, sizeof _b), "NEW CONTENT AT 17:00");
    TEXT(cr_headline(0, 11, 0, 1, _b, sizeof _b), "NEW CONTENT AT 11:00 AM");
    TEXT(cr_headline(1, 1, 0, 0, _b, sizeof _b), "NEW CONTENT TOMORROW AT 01:00");
    TEXT(cr_headline(3, 17, 0, 0, _b, sizeof _b), "NEW CONTENT IN 3 DAYS");

    /* the corner notice's line */
    i = info(4, 3, 1, 0, 0, 0, 0, 0, 0);
    TEXT(cr_toast_text(&i, 500000, 0, 17, 0, 0, _b, sizeof _b), "New content at 17:00 (in 2h 15m): 3 new fighters, 1 skin.");
    TEXT(cr_toast_text(&i, 500000 + 7500, 0, 5, 0, 1, _b, sizeof _b), "New content at 5:00 AM (in 10 min): 3 new fighters, 1 skin.");
    TEXT(cr_toast_text(&i, 500000, 1, 1, 0, 0, _b, sizeof _b), "New content tomorrow at 01:00 (in 2h 15m): 3 new fighters, 1 skin.");

    /* the corner notice: once per schedule in a menu, CR_TOAST_MS long, once more ten minutes before */
    memset(&n, 0, sizeof n);
    CHECK(!cr_toast(&n, &e, 5000, ms, 1));                          /* nothing scheduled */
    CHECK(!cr_toast(&n, &i, 500000, ms, 0));                        /* a fight: nothing, and nothing used up */
    CHECK(cr_toast(&n, &i, 500000, ms, 1));
    CHECK(cr_toast(&n, &i, 500005, ms + 5000, 1));
    CHECK(!cr_toast(&n, &i, 500005, ms + 5000, 0));                 /* hidden in a fight */
    CHECK(!cr_toast(&n, &i, 500013, ms + CR_TOAST_MS + 1, 1));      /* its time is up */
    CHECK(!cr_toast(&n, &i, 503000, ms + 3000000, 1));              /* the same schedule: once */
    CHECK(!cr_toast(&n, &i, 500000 + 8100 - CR_SOON_S - 1, ms + 7499000, 1));
    CHECK(cr_toast(&n, &i, 500000 + 8100 - CR_SOON_S, ms + 7500000, 1));       /* ten minutes before */
    CHECK(!cr_toast(&n, &i, 500000 + 8100 - 300, ms + 7800000, 1));            /* once */
    CHECK(!cr_toast(&n, &i, 500000 + 8100, ms + 8100000, 1));                  /* its time has come: gone */
    i.count = 5;                                                    /* one more pack accepted: said again */
    CHECK(cr_toast(&n, &i, 503000, ms + 9000000, 1));
    memset(&n, 0, sizeof n);                                        /* first heard of five minutes before: one notice */
    CHECK(cr_toast(&n, &i, 500000 + 8100 - 300, ms, 1));
    CHECK(!cr_toast(&n, &i, 500000 + 8100 - 200, ms + 100000, 1));

    /* after the release time: one catalogue check, a minute later plus this PC's spread; never in a fight */
    memset(&n, 0, sizeof n);
    i = info(4, 3, 1, 0, 0, 0, 0, 0, 0);
    CHECK(!cr_after_due(&n, &i, 508100, 0, 0));
    CHECK(!cr_after_due(&n, &i, 508100 + CR_AFTER_S - 1, 0, 0));
    CHECK(!cr_after_due(&n, &i, 508100 + CR_AFTER_S + 30, 90, 0));  /* this PC waits 90 s more */
    CHECK(!cr_after_due(&n, &i, 508100 + CR_AFTER_S + 90, 90, 1));  /* in a fight: later */
    CHECK(cr_after_due(&n, &i, 508100 + CR_AFTER_S + 90, 90, 0));
    CHECK(!cr_after_due(&n, &i, 508100 + CR_AFTER_S + 91, 90, 0));  /* once per release */
    CHECK(!cr_after_due(&n, &e, 999999, 0, 0));                     /* nothing was scheduled */
    i.release_at += 86400;                                          /* the next day's */
    CHECK(!cr_after_due(&n, &i, 508100 + 3600, 0, 0));
    CHECK(cr_after_due(&n, &i, 508100 + 86400 + CR_AFTER_S + CR_AFTER_SPREAD_S, 9999, 0));   /* the spread is capped */

    if (!fails) printf("OK\n");
    return fails ? 1 : 0;
}
'''


@pytest.fixture(scope='module')
def logic_exe(tmp_path_factory):
    compiler = shutil.which('cl')
    if not compiler:
        pytest.skip('the native content release test needs MSVC')
    folder = tmp_path_factory.mktemp('cr')
    (folder / 'driver.c').write_text(DRIVER, encoding='utf-8')
    exe = folder / 'cr.exe'
    r = subprocess.run([compiler, '/nologo', '/W4', '/WX', '/TC', '/I' + str(HOOKS), str(folder / 'driver.c'),
                        str(HOOKS / 'content_release_logic.c'), '/Fe:' + str(exe)],
                       cwd=folder, capture_output=True, text=True)
    assert r.returncode == 0, r.stdout + r.stderr
    return exe


def test_rules_and_texts(logic_exe):
    r = subprocess.run([str(logic_exe)], capture_output=True, text=True)
    assert r.returncode == 0 and r.stdout.strip() == 'OK', r.stdout


def src(name):
    return (ROOT / name).read_text(encoding='utf-8')


def test_kinds_are_the_services_words():
    """The words the notice counts are the ones the account service sends (server: packs_release.go releaseKind:
    new fighter, skin, map, clothing, fighter look, language, pack, update), in the order the notice names them."""
    logic = src('src/hooks/content_release_logic.c')
    table = re.search(r'k_kind\[CR_KINDS\] = \{(.*?)\n\};', logic, re.S).group(1)
    words = re.findall(r'\{ "([^"]+)", "[^"]+", "[^"]+" \}', table)
    assert words == ['new fighter', 'skin', 'map', 'clothing', 'fighter look', 'language', 'pack', 'update']
    order = re.search(r'enum CrKind \{(.*?)\};', src('src/hooks/content_release_logic.h'), re.S).group(1)
    assert re.findall(r'CR_[A-Z_]+', order) == ['CR_NEW_FIGHTER', 'CR_SKIN', 'CR_MAP', 'CR_CLOTHING', 'CR_LOOK',
                                                'CR_LANGUAGE', 'CR_PACK', 'CR_UPDATE', 'CR_KINDS']


def test_asked_with_the_catalogue_check_not_on_a_timer_of_its_own():
    glue = src('src/hooks/content_release.c')
    assert 'account_fetch("/api/packs/next-release"' in glue           # public: no sign-in, the account service's host
    assert 'InterlockedCompareExchange(&g_busy, 1, 0)' in glue          # one request at a time
    assert 'SetTimer' not in glue and 'Sleep(' not in glue
    update = src('src/hooks/content_update.c')
    # build 86's check decides when; a release time that has just passed is one check more (UPDATE NOW takes over)
    assert re.search(r'check = cu_check_due\(g_last_check, now, in_fight, account_state\(\) == ACCOUNT_IN, online_page\);'
                     r'.*?if \(content_release_frame\(check, in_fight\)\) check = 1;\s*if \(check\) \{\s*'
                     r'g_last_check = now;\s*pc_mods_check_start\(\);', update, re.S)
    # the time shown is the PC's own, from the server's clock in the answer
    logic = src('src/hooks/content_release_logic.c')
    assert 'return i->local_time + (i->release_at - i->server_time);' in logic
    assert 'localtime_s' in glue and 'LOCALE_ITIME' in glue


def test_notice_sits_where_update_now_sits():
    screen = src('src/hooks/online_screen.cpp')
    # the online pages' corner badge: after the notices the player has to act on
    title = re.search(r'void page_title\(.*?\n\}\n', screen, re.S).group(0)
    assert title.index('"NEW COMMUNITY CONTENT IS OUT"') < title.index('content_release(head, sizeof(head)')
    assert title.count('corner_plate(dl, f, ') == 4                     # the same badge as the three before it
    ui = src('src/hooks/pc_ui.cpp')
    # the corner notice: after UPDATE NOW's, and only once the main menu is up (the answer often comes during the
    # intro, where 12 s of notice would be over unread)
    assert re.search(r'content_update_toast\(notice, sizeof\(notice\), [^;]*?\) \|\|\s*(//[^\n]*\n\s*)*'
                     r'content_release_toast\(notice, sizeof\(notice\), !g_open\.load\(\) && '
                     r'online_screen::exit_wanted\(\)\);', ui)


def test_gallery_page():
    screen = src('src/hooks/online_screen.cpp')
    assert '"hub-release",' in screen
    assert 'else if (p == "hub-release") { S.page = HUB; S.hub_sel = 0; S.quick_mode = 0; }' in screen
    # the page shows a fixed sample, whatever the clock says; no other gallery page shows the notice
    assert re.search(r'if \(gallery_on\(\)\) \{\s*if \(!\(G\.page && !strcmp\(G\.page, "hub-release"\)\)\) return false;\s*'
                     r'content_release_sample_lines\(head, head_size, detail, detail_size\);', screen)
    glue = src('src/hooks/content_release.c')
    sample = re.search(r'void content_release_sample_lines\(.*?\n\}\n', glue, re.S).group(0)
    assert 'lines(&i, 1000, 0, 17, 0, 0, head, head_size, detail, detail_size);' in sample
