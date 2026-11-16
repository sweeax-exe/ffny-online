"""Row names of appended database rows (setup/ffnycontent/sodwrite.py ``name_above``), build 85 fix (2026-11-16).

A new Character or Clothing row must hash above every row of its table (rows are appended in hash order). The old
``name_above`` took the first suffix whose hash was above the floor, which could be anywhere up to 2**32: each new
fighter used up a random share of the room left, and after about a dozen the search found nothing ("no name above
ffffffff found for 'Incredible'"). Now the name is solved so that its hash is the smallest one above the floor
that a 7-letter tail reaches (almost always floor + 1), never more than ``NAME_WINDOW`` above it."""
import random
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "setup"))
from ffnycontent import clothing, sod, sodwrite  # noqa: E402
sys.path.pop(0)

H = sod.name_hash
CHARACTER_TOP = 0xFBF3CF7B      # the retail database's highest Character row-name hash (75 rows)
CLOTHING_TOP = 0xFF691EF4       # and Clothing's (326 rows)


def test_the_floor_never_jumps_more_than_the_window():
    rng = random.Random(85)
    worst = 0
    for _ in range(5000):
        floor = rng.randrange(0, 0xFFFFFFFF - sodwrite.NAME_WINDOW)
        base = "FFNY_%x" % rng.getrandbits(40)
        name = sodwrite.name_above(base, floor)
        assert floor < H(name) <= floor + sodwrite.NAME_WINDOW, (base, hex(floor), name)
        worst = max(worst, H(name) - floor)
    assert worst < 64           # in practice floor + 1 .. floor + 5


def test_the_hash_is_the_smallest_a_tail_reaches():
    for base, floor in (("Incredible", CHARACTER_TOP), ("Mafia Mo", CHARACTER_TOP + 3), ("FFNY_Tee", CLOTHING_TOP)):
        name = sodwrite.name_above(base, floor)
        assert name.startswith(base + "_") and len(name) == len(base) + 8
        prefix = H(base + "_")
        assert all(sodwrite._tail_for(prefix, t) is None for t in range(floor + 1, H(name)))
        assert sodwrite._tail_for(prefix, H(name)) == name[-7:]


def test_a_base_in_the_window_is_kept_one_far_above_is_not():
    floor = H("Incredible") - 1
    assert sodwrite.name_above("Incredible", floor) == "Incredible"
    far = H("Incredible") - sodwrite.NAME_WINDOW - 1          # the old rule kept it and lost the room between
    assert sodwrite.name_above("Incredible", far) != "Incredible"
    assert H(sodwrite.name_above("Incredible", far)) <= far + sodwrite.NAME_WINDOW


def test_many_rows_fit_and_every_pc_gets_the_same_names():
    """48 fighters on the retail Character top, thousands of clothing items on the Clothing top."""
    def chain(top, bases):
        floor, names = top, []
        for b in bases:
            names.append(sodwrite.name_above(b, floor))
            assert floor < H(names[-1]) <= floor + sodwrite.NAME_WINDOW
            floor = H(names[-1])
        return names, floor
    fighters = ["Incredible", "Mafia Mo", "Mojack", "Robert Parr", "Royalty"] + ["Fighter %d" % i for i in range(43)]
    names, floor = chain(CHARACTER_TOP, fighters)
    assert names == chain(CHARACTER_TOP, fighters)[0] and floor - CHARACTER_TOP < 48 * 8
    items, floor = chain(CLOTHING_TOP, ["FFNY_Item%d" % i for i in range(5000)])
    assert len(set(items)) == 5000 and floor - CLOTHING_TOP < 5000 * 8 < 0xFFFFFFFF - CLOTHING_TOP


def test_no_room_left_is_said_plainly():
    with pytest.raises(ValueError, match="no room left"):
        sodwrite.name_above("Incredible", 0xFFFFFFFF)
    near = sodwrite.name_above("Incredible", 0xFFFFFFF0)        # the last values still work
    assert 0xFFFFFFF0 < H(near) <= 0xFFFFFFFF


def test_clothing_hint_is_kept_only_just_above_the_floor():
    """A pack's row-name hint (made by FFNY Studio against the retail database) is used as it is only when it lands
    in the window; an older Studio's hint far up the range is re-derived from its stem."""
    hint = sodwrite.name_above("FFNY_Tee", CLOTHING_TOP)
    assert clothing.row_name_for(hint, CLOTHING_TOP) == hint
    old = "FFNY_Tee_xxaaaaa"            # what the old name_above made: 0xFFF9ABA9, 9.5 million above the top
    assert H(old) > CLOTHING_TOP + sodwrite.NAME_WINDOW
    again = clothing.row_name_for(old, CLOTHING_TOP)
    assert again.startswith("FFNY_Tee_") and CLOTHING_TOP < H(again) <= CLOTHING_TOP + sodwrite.NAME_WINDOW
    taken = H(hint)                             # a second pack with the same hint goes just above the first
    second = clothing.row_name_for(hint, taken)
    assert second != hint and taken < H(second) <= taken + sodwrite.NAME_WINDOW
