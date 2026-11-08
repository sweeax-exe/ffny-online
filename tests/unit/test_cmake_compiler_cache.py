"""A compile line shared by a whole target must not name the version's own folder.

A compiler cache (#187) hashes the compile line, and its base_dir setting rewrites include paths but not
the text of a definition. DEFJAM_MOD_SOURCE_DIR on the whole game target made all 64 of its files miss in
depend mode in every new version folder (Steam Deck: 69/133 hits, 103 s; on pc_mods.cpp alone, 129/133, 9 s).
"""
import re
from pathlib import Path

CMAKE = (Path(__file__).resolve().parents[2] / 'CMakeLists.txt').read_text(encoding='utf-8')


def target_definitions():
    return re.findall(r'target_compile_definitions\(.*?\)', CMAKE, flags=re.DOTALL)


def test_no_target_wide_definition_names_the_source_folder():
    offenders = [block for block in target_definitions()
                 if 'CMAKE_CURRENT_SOURCE_DIR' in block or 'CMAKE_SOURCE_DIR' in block]
    assert not offenders


def test_the_mods_source_folder_is_defined_on_the_file_that_reads_it():
    assert re.search(r'set_property\(SOURCE src/hooks/pc_mods\.cpp APPEND PROPERTY COMPILE_DEFINITIONS\s+'
                     r'DEFJAM_MOD_SOURCE_DIR=', CMAKE)
