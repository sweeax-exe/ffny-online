"""Synthetic-pack acceptance for signatures, archive safety and recoverable installs."""
import base64
import copy
import datetime as dt
import importlib.util
import io
import json
from pathlib import Path
import shutil
import sys
import zipfile
from unittest.mock import patch

import pytest
from PIL import Image
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'setup'))
spec = importlib.util.spec_from_file_location('defjam_mods', ROOT / 'setup/mods.py')
mods = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mods)
sys.path.pop(0)
ID = 'a' * 64


def package(tmp, name='paper', version='1.0.0', color='red', wide=False):
    content = io.BytesIO()
    Image.new('RGBA', (8, 8), color).save(content, format='PNG')
    image = content.getvalue()
    manifest = f'[pack]\nschema=1\nname=Test\n[textures]\n{ID}=textures/test.png\n'
    entries = {f'{name}/manifest.ini': manifest.encode(), f'{name}/textures/test.png': image}
    if wide:
        entries[f'{name}/manifest.ini'] += f'[wide]\n{ID}=-8:wide/left.png\n'.encode()
        entries[f'{name}/wide/left.png'] = image
    path = tmp / f'{name}-{version}.zip'
    with zipfile.ZipFile(path, 'w') as archive:
        for key, data in entries.items():
            archive.writestr(key, data)
    descriptor = dict(root=name, size=path.stat().st_size, sha256=mods.sha(path),
                      unpacked_size=sum(map(len, entries.values())), file_count=len(entries),
                      urls=['https://example.invalid/test.zip'])
    mod = dict(id=name, name='Paper test', description='Synthetic fixture', authors=['Test'],
               licence='Test fixture', homepage='https://example.invalid', **{'class': 'original'},
               revoked_versions=[], versions=[dict(version=version, channel='stable', min_recomp='0.1.0',
               capabilities=['textures-v1'] + (['wide-v1'] if wide else []), reviewed='2026-10-21',
               depends=[], conflicts=[], load_after=[], archive=descriptor)])
    return path, mod


def signed(manager, key, rows, revision=1, issued=None, expires=None):
    now = dt.datetime.now(dt.timezone.utc)
    issued = issued or (now-dt.timedelta(minutes=1)).strftime('%Y-%m-%dT%H:%M:%SZ')
    expires = expires or (now+dt.timedelta(days=2)).strftime('%Y-%m-%dT%H:%M:%SZ')
    raw = mods.contract.build(rows, revision, issued, expires)
    signature = key.sign(raw)
    mods.atomic_json(manager.root/'catalogue.json', dict(raw=base64.b64encode(raw).decode(),
        signature=base64.b64encode(signature).decode(), revision=revision,
        sha256=mods.hashlib.sha256(raw).hexdigest()))
    return raw, signature


@pytest.fixture
def manager(tmp_path):
    key = Ed25519PrivateKey.generate()
    manager = mods.Manager(tmp_path/'data', '0.10.6', ['textures-v1', 'wide-v1'],
                           keys=[key.public_key().public_bytes_raw().hex()])
    return manager, key


def test_view_projects_all_catalogue_authors(manager,tmp_path):
    manager,key=manager
    _,row=package(tmp_path)
    row['authors']=['First Author','Second Author']
    signed(manager,key,[row])
    manager.view()
    assert 'authors=First Author, Second Author\n' in (manager.root/'view.ini').read_text()


def test_signed_index_trust_and_replay(manager, tmp_path):
    manager, key = manager
    _, row = package(tmp_path)
    raw, signature = signed(manager, key, [row])
    assert manager.trusted(True)['mods'][0]['id'] == 'paper'
    with pytest.raises(ValueError, match='signature'):
        mods.verify_index(raw+b' ', signature, manager.keys)
    with pytest.raises(ValueError, match='rollback'):
        mods.verify_index(raw, signature, manager.keys, highest=2)
    with pytest.raises(ValueError, match='without a new revision'):
        mods.verify_index(raw, signature, manager.keys, highest=1, digest='f'*64)
    with pytest.raises(ValueError, match='untrusted'):
        mods.verify_index(raw, signature, [])


def test_expired_metadata_can_display_but_not_install(manager, tmp_path):
    manager, key = manager
    _, row = package(tmp_path)
    signed(manager, key, [row], issued='2020-01-01T00:00:00Z', expires='2020-01-02T00:00:00Z')
    assert manager.trusted()['mods']
    with pytest.raises(ValueError, match='expired'):
        manager.choose('paper')
    manager.view()
    assert 'expired' in (manager.root/'view.ini').read_text()


def test_install_update_rollback_remove_preserve_unmanaged(manager, tmp_path):
    manager, key = manager
    local = manager.mods/'faithful-hd'; local.mkdir(); (local/'keep').write_text('own dump')
    first, row = package(tmp_path)
    signed(manager, key, [row]); manager.install('paper', archive_path=first)
    assert (manager.mods/'paper/textures/test.png').exists()
    assert not (manager.data/'settings.ini').exists()  # no hidden enable/reorder
    second, newer = package(tmp_path, version='1.1.0', color='blue')
    row['versions'] += newer['versions']
    signed(manager, key, [row], revision=2); manager.install('paper', archive_path=second)
    assert manager.state()['installed']['paper']['version'] == '1.1.0'
    assert manager.state()['installed']['paper']['previous']['version'] == '1.0.0'
    manager.rollback('paper')
    assert manager.state()['installed']['paper']['version'] == '1.0.0'
    manager.remove('paper')
    assert not (manager.mods/'paper').exists()
    assert (local/'keep').read_text() == 'own dump'


def test_local_collision_and_modified_managed_files_are_preserved(manager, tmp_path):
    manager, key = manager
    source, row = package(tmp_path); signed(manager, key, [row])
    target = manager.mods/'paper'; target.mkdir(); (target/'local.txt').write_text('local')
    with pytest.raises(ValueError, match='local folder'):
        manager.install('paper', archive_path=source)
    assert (target/'local.txt').read_text() == 'local'
    shutil.rmtree(target); manager.install('paper', archive_path=source)
    (target/'manifest.ini').write_text('local changes')
    with pytest.raises(ValueError, match='local edits'):
        manager.remove('paper')
    assert (target/'manifest.ini').read_text() == 'local changes'


def test_invalid_hash_cancel_and_game_lock_do_not_install(manager, tmp_path):
    manager, key = manager
    source, row = package(tmp_path); signed(manager, key, [row])
    with mods.file_lock(manager.root/'runtime.lock', shared=True):
        with pytest.raises(ValueError, match='game is running'):
            manager.install('paper', archive_path=source)
    assert not (manager.mods/'paper').exists()
    cancel = manager.root/'cancel'; cancel.touch(); manager.cancel_file=cancel
    with pytest.raises(ValueError, match='cancelled'):
        manager.install('paper', archive_path=source)
    assert not (manager.mods/'paper').exists()
    manager.cancel_file=None
    source.write_bytes(source.read_bytes()+b'changed')
    with pytest.raises(ValueError, match='SHA-256'):
        manager.install('paper', archive_path=source)
    assert not (manager.mods/'paper').exists()


@pytest.mark.parametrize('bad', ['../escape.png','paper/../escape.png','/absolute.png','paper/C:foo.png',
                               'paper/CON.png','paper/a//b.png','paper/a./b.png','paper/evil.exe'])
def test_archive_paths_and_extensions_rejected(tmp_path, bad):
    source, row = package(tmp_path)
    with zipfile.ZipFile(source,'a') as archive: archive.writestr(bad,b'bad')
    descriptor=row['versions'][0]['archive']; descriptor.update(size=source.stat().st_size,sha256=mods.sha(source),
        unpacked_size=descriptor['unpacked_size']+3,file_count=descriptor['file_count']+1)
    with pytest.raises(ValueError): mods.unpack_archive(source,descriptor,tmp_path/'out')
    assert not (tmp_path/'out').exists()


@pytest.mark.parametrize('kind',['symlink','case','size','missing','wide'])
def test_archive_layout_and_manifest_rejected(tmp_path,kind):
    source,row=package(tmp_path)
    with zipfile.ZipFile(source) as archive:
        files={r.filename:archive.read(r) for r in archive.infolist()}
    if kind=='symlink':
        extra=zipfile.ZipInfo('paper/evil.png');extra.external_attr=(0o120777<<16)
    elif kind=='case': extra='paper/textures/TEST.PNG'
    else: extra=None
    if kind=='missing': files['paper/manifest.ini']=files['paper/manifest.ini'].replace(b'test.png',b'absent.png')
    if kind=='wide': files['paper/manifest.ini']+=f'[wide]\n{ID}=garbage\n'.encode()
    with zipfile.ZipFile(source,'w') as archive:
        for name,data in files.items():archive.writestr(name,data)
        if extra:archive.writestr(extra,b'bad')
    descriptor=row['versions'][0]['archive'];descriptor.update(size=source.stat().st_size,sha256=mods.sha(source),
        unpacked_size=sum(map(len,files.values()))+(3 if extra else 0),file_count=len(files)+(1 if extra else 0))
    if kind=='size': descriptor['unpacked_size']+=1
    with pytest.raises(ValueError):mods.unpack_archive(source,descriptor,tmp_path/'out')


def test_wide_capability_and_semver(manager,tmp_path):
    manager,key=manager
    source,row=package(tmp_path,wide=True);signed(manager,key,[row])
    manager.capabilities={'textures-v1'}
    with pytest.raises(ValueError,match='compatible'):manager.install('paper',archive_path=source)
    manager.capabilities.add('wide-v1');manager.install('paper',archive_path=source)
    assert manager.state()['installed']['paper']['wide_ids']==[ID]
    assert mods.semver('1.0.0-rc.2') < mods.semver('1.0.0-rc.10') < mods.semver('1.0.0')


def test_revocation_dependencies_and_conflicts(manager,tmp_path):
    manager,key=manager
    source,row=package(tmp_path);row['revoked_versions']=['1.0.0'];signed(manager,key,[row])
    with pytest.raises(ValueError,match='compatible'):manager.install('paper',archive_path=source)
    row['revoked_versions']=[];row['versions'][0]['depends']=['missing'];signed(manager,key,[row],revision=2)
    with pytest.raises(ValueError,match='dependencies'):manager.install('paper',archive_path=source)


def test_recover_after_old_folder_moved(manager,tmp_path):
    manager,key=manager
    source,row=package(tmp_path);signed(manager,key,[row]);manager.install('paper',archive_path=source)
    old=manager.state()['installed']['paper']
    newer=copy.deepcopy(old);newer['version']='2.0.0';newer['files']={}
    mods.atomic_json(manager.root/'journal.json',dict(id='paper',old=old,new=newer))
    (manager.mods/'paper').rename(manager.backups/'paper')
    manager.recover()
    assert manager.unchanged(manager.mods/'paper',old)
    assert manager.state()['installed']['paper']['version']=='1.0.0'
    assert not (manager.root/'journal.json').exists()


def test_recover_after_new_folder_committed_before_state(manager,tmp_path):
    manager,key=manager
    source,row=package(tmp_path);signed(manager,key,[row]);manager.install('paper',archive_path=source)
    old=manager.state()['installed']['paper']
    newer=copy.deepcopy(old);newer['version']='2.0.0'
    mods.atomic_json(manager.root/'journal.json',dict(id='paper',old=old,new=newer))
    manager.recover()
    assert manager.state()['installed']['paper']['version']=='2.0.0'


class Response(io.BytesIO):
    def __init__(self,data,status=200,headers=None):
        super().__init__(data);self.status=status;self.headers=headers or {'Content-Length':str(len(data))}


@pytest.mark.parametrize('range_honored',[True,False])
def test_resume_and_server_ignoring_range(manager,tmp_path,range_honored):
    manager,key=manager
    source,row=package(tmp_path);descriptor=row['versions'][0]['archive'];data=source.read_bytes();offset=50
    (manager.cache/(descriptor['sha256']+'.part')).write_bytes(data[:offset])
    def response(url,headers):
        assert headers['Range']==f'bytes={offset}-'
        return (Response(data[offset:],206,{'Content-Range':f'bytes {offset}-{len(data)-1}/{len(data)}',
                    'Content-Length':str(len(data)-offset)}) if range_honored else Response(data))
    with patch.object(mods,'open_url',side_effect=response):
        result=manager.download(descriptor)
    assert result.read_bytes()==data


def test_bad_range_hash_and_size_rejected(manager,tmp_path):
    manager,key=manager
    source,row=package(tmp_path);descriptor=row['versions'][0]['archive'];data=source.read_bytes()
    with patch.object(mods,'open_url',return_value=Response(data,206,{'Content-Range':'garbage','Content-Length':str(len(data))})):
        with pytest.raises(ValueError,match='range'):manager.download(descriptor)
    with patch.object(mods,'open_url',return_value=Response(b'x'*len(data))):
        with pytest.raises(ValueError,match='SHA-256'):manager.download(descriptor)


def test_empty_trust_offline_status_never_changes_packs(tmp_path):
    manager=mods.Manager(tmp_path,keys=[])
    manager.view()
    assert 'No verified catalogue' in (manager.root/'view.ini').read_text()
    with pytest.raises(ValueError,match='provisioned'):manager.refresh()
    assert not list(manager.mods.iterdir())


def test_unowned_and_edited_rollback_directory_is_preserved(manager,tmp_path):
    manager,key=manager
    source,row=package(tmp_path);signed(manager,key,[row])
    backup=manager.backups/'paper';backup.mkdir();(backup/'unowned.txt').write_text('keep')
    with pytest.raises(ValueError,match='Rollback folder'):
        manager.install('paper',archive_path=source)
    assert (backup/'unowned.txt').read_text()=='keep'
    shutil.rmtree(backup);manager.install('paper',archive_path=source)
    new,newer=package(tmp_path,version='1.1.0',color='blue')
    row['versions']+=newer['versions'];signed(manager,key,[row],revision=2);manager.install('paper',archive_path=new)
    (backup/'notes.txt').write_text('user notes')
    with pytest.raises(ValueError,match='Rollback folder'):manager.remove('paper')
    assert (backup/'notes.txt').read_text()=='user notes'


def test_recovery_refuses_unrecognized_target_and_preserves_journal(manager,tmp_path):
    manager,key=manager
    source,row=package(tmp_path);signed(manager,key,[row]);manager.install('paper',archive_path=source)
    old=manager.state()['installed']['paper']
    mods.atomic_json(manager.root/'journal.json',dict(id='paper',old=old,new=None))
    (manager.mods/'paper/notes.txt').write_text('changed')
    with pytest.raises(ValueError,match='needs recovery'):manager.recover()
    assert (manager.root/'journal.json').exists()
    assert (manager.mods/'paper/notes.txt').read_text()=='changed'


def test_writer_lock_failure_does_not_overwrite_progress(manager):
    manager,key=manager
    progress=manager.root/'progress.ini';progress.write_text('active writer')
    with mods.file_lock(manager.root/'writer.lock'),patch('sys.argv',['mods','status','--data-dir',str(manager.data)]):
        assert mods.main()==1
    assert progress.read_text()=='active writer'


def test_durable_journal_precedes_folder_mutation(manager,tmp_path):
    manager,key=manager
    source,row=package(tmp_path);signed(manager,key,[row])
    real=mods.durable_rename;events=[]
    def rename(src,dst):
        if Path(dst)==manager.mods/'paper':
            assert (manager.root/'journal.json').exists()
            events.append('install')
        return real(src,dst)
    with patch.object(mods,'durable_rename',side_effect=rename):manager.install('paper',archive_path=source)
    assert events==['install']


def test_original_overlay_stretch_does_not_require_replacement_image(tmp_path):
    source,row=package(tmp_path)
    with zipfile.ZipFile(source) as archive:entries={r.filename:archive.read(r) for r in archive.infolist()}
    entries['paper/manifest.ini']+=('[wide]\n'+'b'*64+'=stretch\n').encode()
    with zipfile.ZipFile(source,'w') as archive:
        for name,data in entries.items():archive.writestr(name,data)
    descriptor=row['versions'][0]['archive'];descriptor.update(size=source.stat().st_size,sha256=mods.sha(source),unpacked_size=sum(map(len,entries.values())))
    result=mods.unpack_archive(source,descriptor,tmp_path/'stage')
    assert result['wide_ids']==['b'*64]


@pytest.mark.parametrize('name',['mods','mod-manager'])
def test_redirected_manager_or_mods_root_is_not_followed(tmp_path,name):
    outside=tmp_path/'outside';outside.mkdir();data=tmp_path/'data';data.mkdir()
    if sys.platform=='win32':
        import _winapi
        _winapi.CreateJunction(str(outside),str(data/name))
    else:(data/name).symlink_to(outside,target_is_directory=True)
    with pytest.raises(ValueError,match='symlink'):mods.Manager(data,keys=[])
    assert list(outside.iterdir())==[]


def test_oversized_receipt_is_refused_before_pack_switch(manager, tmp_path):
    manager, key = manager
    archive, row = package(tmp_path)
    signed(manager, key, [row])
    manager.install('paper', archive_path=archive)
    old = manager.state()['installed']['paper']
    stage = manager.staging/'synthetic-large-receipt'
    stage.mkdir()
    new = dict(old, description='x' * 8192)
    with patch.object(mods, 'MAX_INDEX', 4096), pytest.raises(ValueError, match='size limit'):
        manager.commit('paper', stage, new)
    assert manager.unchanged(manager.mods/'paper', old)
    assert manager.state()['installed']['paper'] == old
    assert not (manager.root/'journal.json').exists()


def test_testing_channel_is_explicit_for_view_and_install(manager, tmp_path):
    manager, key = manager
    archive, row = package(tmp_path, version='1.0.0-rc2')
    row['versions'][0]['channel'] = 'prerelease'
    signed(manager, key, [row])
    manager.view()
    assert 'compatible=0' in (manager.root/'view.ini').read_text()
    with pytest.raises(ValueError, match='compatible'):
        manager.choose('paper')
    manager.prerelease = True
    manager.view()
    assert 'channel=prerelease' in (manager.root/'view.ini').read_text()
    assert 'compatible=1' in (manager.root/'view.ini').read_text()
    manager.install('paper', archive_path=archive)
    assert manager.state()['installed']['paper']['version'] == '1.0.0-rc2'
    manager.prerelease = False
    manager.view()
    assert manager.state()['installed']['paper']['version'] == '1.0.0-rc2'
    assert 'compatible=0' in (manager.root/'view.ini').read_text()
