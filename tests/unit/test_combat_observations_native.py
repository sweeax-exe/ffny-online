"""Synthetic fighter records verify read-only probes without licensed data."""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import pytest

import native_hooks_fixtures

ROOT = Path(__file__).resolve().parents[2]


DRIVER = r'''
#include <stdint.h>
#include <string.h>
#include "test_observations.h"
unsigned char memory[0x400000];
int main(void) {
    static unsigned char before[sizeof(memory)];
    float initial=100, after=90;
    uint32_t actor=0x3b92e0;
    memcpy(memory+actor+0x350,&initial,4);
    memcpy(before,memory,sizeof(memory));
    defjam_test_update();
    if (!defjam_test_observations_enabled())
        return memcmp(memory,before,sizeof(memory)) != 0;
    /* Invalid/unrelated objects are rejected before any memory read. */
    defjam_test_health_begin(0xffffffffu,1);
    defjam_test_health_notify(0xffffffffu,0x3d);
    defjam_test_hit(actor+1,actor,2);
    defjam_test_health_begin(actor,3);
    defjam_test_health_notify(actor+0x12e8,0x3d);
    defjam_test_health_notify(actor,0x3a);
    if(memcmp(memory,before,sizeof(memory))) return 1;
    defjam_test_hit(actor+0x12e8,actor,4);
    memcpy(memory+actor+0x350,&after,4);
    memcpy(before,memory,sizeof(memory));
    defjam_test_health_notify(actor,0x3d);
    /* Duplicate notifications cannot report the same write again. */
    defjam_test_health_notify(actor,0x3d);
    return memcmp(memory,before,sizeof(memory)) != 0;
}
'''


@pytest.fixture(scope='module')
def observation_tool(tmp_path_factory):
    # The large comparison buffer is static: off the default Windows 1 MB stack.
    return native_hooks_fixtures.build(tmp_path_factory, 'observations-native', DRIVER)


def test_observations_off_by_default(observation_tool):
    env = dict(os.environ, RECOMP_TEST_OBSERVATIONS='0')
    result = subprocess.run([str(observation_tool)], env=env, capture_output=True, text=True)
    assert result.returncode == 0 and not result.stderr


def test_health_probes_read_only_and_match_exact_actor(observation_tool):
    env = dict(os.environ, RECOMP_TEST_OBSERVATIONS='1')
    result = subprocess.run([str(observation_tool)], env=env, capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    events = [json.loads(line.split('] ', 1)[1]) for line in result.stderr.splitlines()]
    assert [e['kind'] for e in events] == ['hit_resolution', 'health_change']
    assert events[0]['defender_slot'] == events[1]['slot'] == 0
    assert events[1]['before'] == 100 and events[1]['after'] == 90 and events[1]['update'] == 1
    assert all('frame' not in e for e in events)


def test_health_observations_cannot_link_across_updates_or_actors():
    spec = importlib.util.spec_from_file_location('evidence', ROOT/'scripts/test_evidence.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    records = [dict(kind='hit_resolution', update=1, defender_slot=0),
               dict(kind='health_change', update=2, slot=0, before=100, after=90),
               dict(kind='health_change', update=1, slot=1, before=100, after=90)]
    log = lambda: '\n'.join('[TEST-OBSERVATION] '+json.dumps(e) for e in records)
    assert not module.health_observations(log())['linked_health_decreases']
    records.append(dict(kind='health_change', update=1, slot=0, before=100, after=90))
    assert len(module.health_observations(log())['linked_health_decreases']) == 1
    assert module.combat_checks(log())[0]['status'] == 'blocked'
