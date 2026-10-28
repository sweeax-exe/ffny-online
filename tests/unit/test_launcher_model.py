"""Native launcher inventory and application-timing behavior, without ImGui or game data."""
from pathlib import Path
import shutil
import subprocess
import pytest

ROOT=Path(__file__).resolve().parents[2]
DRIVER=r'''
#include "pc_ui_model.h"
#include <iostream>
#if defined(_WIN32)
int wmain(int argc,wchar_t**argv){
    auto list=pcui::inventory(std::filesystem::path(argv[1]),
                            pcui::path_utf8(std::filesystem::path(argv[2])));
#else
int main(int argc,char**argv){
    auto list=pcui::inventory(std::filesystem::u8path(argv[1]),argv[2]);
#endif
#if defined(_WIN32)
    auto mods=std::filesystem::path(argv[1]);
    std::string action=argc>3?pcui::path_utf8(std::filesystem::path(argv[3])):"";
#else
    auto mods=std::filesystem::u8path(argv[1]);
    std::string action=argc>3?argv[3]:"";
#endif
    auto groups=pcui::mod_groups(mods,list);
    if(argc>3 && action=="toggle")
        for(const auto &g:groups)if(g.builtin)pcui::select_mod(list,g,true);
    if(argc>3 && action=="lower")pcui::move_priority(list,groups,0,false);
    if(argc>3 && action=="higher")pcui::move_priority(list,groups,1,true);
    groups=pcui::mod_groups(mods,list);
    for(const auto &g:groups)if(g.builtin)std::cout<<"builtin="<<g.name<<"|"<<g.members.size()<<"\n";
    for(const auto &g:pcui::priority_groups(list,groups))std::cout<<"priority="<<g.name<<"|"<<g.members.size()<<"\n";
    for(const auto&p:list)std::cout<<p.name<<"|"<<p.selected<<"|"<<p.problem<<"\n";
    for(const auto &pair:pcui::pack_conflicts(mods,list))std::cout<<"conflict="<<pair.first<<"|"<<pair.second<<"\n";
    std::cout<<"chosen="<<pcui::selected_packs(list)<<"\n";
    std::cout<<"timing="<<pcui::aspect_next_launch(0,1)<<pcui::aspect_next_launch(1,0)
        <<pcui::aspect_next_launch(0,2)<<pcui::aspect_next_launch(2,0)
        <<pcui::aspect_next_launch(2,3)<<pcui::aspect_next_launch(3,3)<<"\n";
}
'''

@pytest.fixture(scope='module', params=['c++17', 'c++20'])
def inventory_tool(tmp_path_factory, request):
    folder=tmp_path_factory.mktemp('launcher-model')
    source=folder/'main.cpp';source.write_text(DRIVER)
    compiler=shutil.which('cl') or shutil.which('clang++') or shutil.which('g++')
    if not compiler:pytest.skip('C++ compiler required')
    exe=folder/('inventory.exe' if Path(compiler).stem.lower()=='cl' else 'inventory')
    if Path(compiler).stem.lower()=='cl':
        cmd=[compiler,'/nologo','/std:' + request.param,'/EHsc','/I'+str(ROOT/'src/hooks'),str(source),'/Fe:'+str(exe)]
    else:cmd=[compiler,'-std=' + request.param,'-I'+str(ROOT/'src/hooks'),str(source),'-o',str(exe)]
    result=subprocess.run(cmd,cwd=folder,capture_output=True,text=True)
    assert result.returncode==0,result.stdout+result.stderr
    return exe

def run(tool,mods,chosen,action=None):
    result=subprocess.run([str(tool),str(mods),chosen]+([action] if action else []),capture_output=True,encoding='utf-8',check=True)
    return result.stdout

def pack(mods,name,manifest='[pack]\nschema = 1\n[textures]\n'):
    folder=mods/name;folder.mkdir(parents=True)
    if manifest is not None:(folder/'manifest.ini').write_text(manifest,encoding='utf-8')

def test_missing_packs_and_priority_are_preserved(inventory_tool,tmp_path):
    pack(tmp_path,'first');pack(tmp_path,'last')
    out=run(inventory_tool,tmp_path,'last;missing;first')
    assert 'chosen=last;missing;first\n' in out
    assert 'missing|1|Not installed' in out
    assert 'last|1|\n' in out

def test_inventory_adds_available_packs_without_enabling_them(inventory_tool,tmp_path):
    pack(tmp_path,'new');pack(tmp_path,'chosen')
    out=run(inventory_tool,tmp_path,'chosen')
    assert 'chosen|1|\n' in out and 'new|0|\n' in out
    assert 'chosen=chosen\n' in out

def test_invalid_names_and_unsupported_headers_are_reported(inventory_tool,tmp_path):
    pack(tmp_path,'future','[pack]\nschema=2\n');pack(tmp_path,'broken',None)
    out=run(inventory_tool,tmp_path,'../outside;future;broken')
    assert '../outside|1|Invalid pack folder name' in out
    assert 'future|1|Unsupported manifest schema' in out
    assert 'broken|1|Missing manifest.ini' in out

def test_bom_and_whitespace_headers_and_duplicate_selection(inventory_tool,tmp_path):
    pack(tmp_path,'valid','\ufeff[pack]\n  schema = 1 \n[textures]\n')
    out=run(inventory_tool,tmp_path,' valid ;valid; ;valid')
    assert 'valid|1|\n' in out and 'chosen=valid\n' in out

def test_missing_mods_directory_is_not_an_error(inventory_tool,tmp_path):
    out=run(inventory_tool,tmp_path/'absent','')
    assert out.startswith('chosen=\n')

def test_aspect_timing_distinguishes_live_shapes_from_widescreen(inventory_tool,tmp_path):
    assert 'timing=001110\n' in run(inventory_tool,tmp_path,'')

def test_duplicate_pack_list_preserves_last_override_priority(inventory_tool, tmp_path):
    pack(tmp_path, 'first')
    pack(tmp_path, 'second')
    out = run(inventory_tool, tmp_path, 'first;second;first')
    assert 'chosen=second;first\n' in out


def test_conflict_readout_names_the_final_winner(inventory_tool,tmp_path):
    key='a'*64
    for name in ('base','middle','menu'):
        pack(tmp_path,name,'[pack]\nschema=1\n[textures]\n'+key+'=test.png\n')
    out=run(inventory_tool,tmp_path,'base;middle;menu')
    assert 'conflict=base|1 texture(s) overridden by menu' in out
    assert 'conflict=middle|1 texture(s) overridden by menu' in out
    assert 'conflict=menu|\n' in out


def generated_hd(mods, policy, recipe='a'*64):
    import json
    name=f'faithful-hd-{recipe[:12]}-{policy}'
    pack(mods,name,f'[pack]\nschema=1\npng_mips={policy}\n[textures]\n')
    (mods/name/'builder.json').write_text(json.dumps(dict(schema=1,product='DefJamFaithfulHD',recipe_id=recipe,png_mips=policy),indent=2))
    return name


def test_builtin_group_preserves_saved_order_and_missing_legacy_alias(inventory_tool,tmp_path):
    opacity=generated_hd(tmp_path,'opacity');channels=generated_hd(tmp_path,'channels')
    pack(tmp_path,'custom')
    chosen=f'faithful-hd;{opacity};{channels};custom'
    out=run(inventory_tool,tmp_path,chosen)
    assert 'builtin=Recomp HD textures|2\n' in out
    assert f'chosen={chosen}\n' in out
    assert 'faithful-hd|1|Not installed' in out
    assert out.index('priority=custom')<out.index('priority=Recomp HD textures')<out.index('priority=faithful-hd')


def test_priority_move_moves_whole_contiguous_builtin_pair(inventory_tool,tmp_path):
    opacity=generated_hd(tmp_path,'opacity');channels=generated_hd(tmp_path,'channels')
    pack(tmp_path,'custom')
    for action in ('lower','higher'):
        out=run(inventory_tool,tmp_path,f'{opacity};{channels};custom',action)
        assert f'chosen=custom;{opacity};{channels}\n' in out


def test_interleaved_components_keep_distinct_priorities(inventory_tool,tmp_path):
    opacity=generated_hd(tmp_path,'opacity');channels=generated_hd(tmp_path,'channels')
    pack(tmp_path,'custom')
    chosen=f'{opacity};custom;{channels}'
    out=run(inventory_tool,tmp_path,chosen)
    assert 'builtin=Recomp HD textures|2\n' in out
    assert 'priority=Recomp HD textures (material channels)|1' in out
    assert 'priority=Recomp HD textures (transparency)|1' in out
    assert f'chosen={chosen}\n' in out


def test_group_toggle_keeps_existing_slot_priority(inventory_tool,tmp_path):
    opacity=generated_hd(tmp_path,'opacity');channels=generated_hd(tmp_path,'channels')
    pack(tmp_path,'custom')
    out=run(inventory_tool,tmp_path,f'{opacity};custom','toggle')
    assert f'chosen={opacity};custom;{channels}\n' in out


def test_lookalike_or_mismatched_marker_is_not_a_builtin(inventory_tool,tmp_path):
    import json
    for name in ('faithful-hd-fake-channels','faithful-hd'):
        pack(tmp_path,name)
    opacity=generated_hd(tmp_path,'opacity')
    marker=tmp_path/opacity/'builder.json'
    value=json.loads(marker.read_text());value['png_mips']='channels';marker.write_text(json.dumps(value))
    assert 'builtin=' not in run(inventory_tool,tmp_path,'')
