"""Build 95: [online] fighter keeps a community fighter's number (the fighter-255 crash of builds 93/94).

The toolkit's settings clamp an INT to its range (tools/xboxrecomp/src/settings/recomp_settings.c valid_value). With the
range 0..255 that builds 1-94 gave [online] fighter, picking community fighter 1008 saved 255; the room screen's A then
sent READY with fighter 255, the relay played its "no fighter" and START carried 255 to every PC, which crashed. This
compiles the real settings code with the entry pc_settings.c has now and with the old one. Needs MSVC (`cl`)."""
import re
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
SETTINGS = ROOT / "tools" / "xboxrecomp" / "src" / "settings"

DRIVER = r'''
#include <stdio.h>
#include "recomp_settings.h"

static RecompSetting now_table[] = {
    { "online", "fighter", RECOMP_SETTING_INT, 55, 0, MAXV, NULL, NULL, 0, "test" },
};
static RecompSetting old_table[] = {
    { "online", "fighter", RECOMP_SETTING_INT, 55, 0, 255, NULL, NULL, 0, "test" },
};

static void run(const char *name, RecompSetting *t)
{
    int picks[] = { 30, 99, 1000, 1008, 4242 }, i;
    recomp_settings_reset();
    recomp_settings_register(t, 1);
    for (i = 0; i < 5; i++) {
        recomp_settings_set("online", "fighter", picks[i]);
        printf("%s|%d|%d\n", name, picks[i], recomp_settings_get("online", "fighter", -1));
    }
}

int main(void)
{
    run("now", now_table);
    run("old", old_table);
    return 0;
}
'''


def entry_max():
    src = (ROOT / "src" / "hooks" / "pc_settings.c").read_text(encoding="utf-8")
    m = re.search(r'\{ "online", "fighter", RECOMP_SETTING_INT, 55, 0, (\d+),', src)
    assert m
    return int(m.group(1))


@pytest.fixture(scope="module")
def rows(tmp_path_factory):
    compiler = shutil.which("cl")
    if not compiler:
        pytest.skip("the native fixture needs MSVC")
    if not (SETTINGS / "recomp_settings.c").is_file():
        pytest.skip("the toolkit submodule is not checked out")
    folder = tmp_path_factory.mktemp("fsetting")
    (folder / "driver.c").write_text(DRIVER, encoding="utf-8")
    exe = folder / "fsetting.exe"
    built = subprocess.run([compiler, "/nologo", "/O2", "/TC", "/utf-8", "/DMAXV=%d" % entry_max(),
                            "/D_CRT_SECURE_NO_WARNINGS", "/I" + str(SETTINGS), str(folder / "driver.c"),
                            str(SETTINGS / "recomp_settings.c"), "/Fe:" + str(exe)],
                           cwd=folder, capture_output=True, text=True)
    assert built.returncode == 0, built.stdout + built.stderr
    out = subprocess.run([str(exe)], capture_output=True, check=True).stdout.decode("utf-8")
    return {(a, int(b)): int(c) for a, b, c in (r.split("|") for r in out.splitlines())}


def test_numbers_are_kept(rows):
    for pick in (30, 99, 1000, 1008, 4242):
        assert rows[("now", pick)] == pick, pick


def test_the_old_range_made_them_255(rows):
    # the cause: every number became 255, the relay's "no fighter"
    for pick in (1000, 1008, 4242):
        assert rows[("old", pick)] == 255, pick
    assert rows[("old", 30)] == 30
