"""Real Windows executable locks and the handoff from an already released waiting wrapper.

The native fixture is game-free and links the production handoff implementation. POSIX
also compiles it to check that exec-based wrappers need no Windows lifecycle change.
"""
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import time

import pytest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('handoff_engine', ROOT / 'setup/engine.py')
engine = importlib.util.module_from_spec(spec)
spec.loader.exec_module(engine)

DRIVER = r'''
#include "app_setup_handoff.h"
#include <fstream>
#include <string>
#if defined(_WIN32)
#include <windows.h>
#endif
namespace fs = std::filesystem;
int main(int argc, char **argv) {
    std::string error;
    unsigned long timeout = 5000;
    if (argc >= 5 && std::string(argv[1]) == "--setup-handoff") {
        // The timeout fixture leaves its wrapper running after the original app exits.
        bool ok = app::os::wait_setup_handoff(argv[2], argv[3], argv[4], &error,
                                             argc > 8 && std::string(argv[8]) == "timeout" ? 100 : timeout);
        for (int i=1; i+4<argc; ++i) argv[i]=argv[i+4];
        argc-=4;
        if (!ok) { std::ofstream(argv[3]) << error; return 0; }
    }
    if (argc < 4) return 10;
    std::string mode=argv[1]; fs::path root=argv[2], report=argv[3];
#if defined(_WIN32)
    if (mode == "hold") { std::ofstream(report) << "holding"; Sleep(15000); return 0; }
    if (mode == "legacy") {
        std::wstring exe=(fs::path(argv[0]).parent_path()/"fixture.exe").wstring();
        std::wstring command=L"\""+exe+L"\" app \""+root.wstring()+L"\" \""+report.wstring()+L"\" "+fs::path(argv[4]).wstring();
        STARTUPINFOW si{}; si.cb=sizeof si; PROCESS_INFORMATION pi{};
        if (!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi)) return 11;
        CloseHandle(pi.hThread); WaitForSingleObject(pi.hProcess,10000); CloseHandle(pi.hProcess);
        if (std::string(argv[4]) == "timeout") Sleep(1000);
        return 0;
    }
    if (mode == "resumed") {
        if (argc != 9 || std::string(argv[5]) != "" || std::string(argv[6]) != "dump & \"quoted\"" ||
            std::string(argv[7]) != "D:\\Games\\dump\\" || std::string(argv[8]) != "\xc3\xbc") {
            std::ofstream(report) << "resume arguments changed"; return 13;
        }
        bool moved=MoveFileExW((root/"DefJamLauncher.exe.new").c_str(),(root/"DefJamLauncher.exe").c_str(),MOVEFILE_REPLACE_EXISTING);
        std::ofstream(report) << (moved ? "replaced" : "locked"); return 0;
    }
#endif
    std::string next = std::string(argv[4]) == "spawnfail" ? (root/"missing.exe").string() : argv[0];
    auto result=app::os::setup_handoff(root,{next,"resumed",root.string(),report.string(),argv[4],
                                           "", "dump & \"quoted\"", "D:\\Games\\dump\\", "\xc3\xbc"},&error);
    if (result==app::os::SetupHandoff::Failed) { std::ofstream(report)<<error; return 0; }
    if (result==app::os::SetupHandoff::Unneeded) std::ofstream(report)<<"unneeded";
    return 0;
}
'''


@pytest.fixture(scope='module')
def native(tmp_path_factory):
    folder = tmp_path_factory.mktemp('setup-handoff-native')
    source = folder / 'fixture.cpp'
    source.write_text(DRIVER)
    binary = folder / ('fixture.exe' if os.name == 'nt' else 'fixture')
    compiler = shutil.which('cl' if os.name == 'nt' else 'c++')
    if not compiler:
        pytest.skip('native C++ compiler required')
    args = ([compiler, '/nologo', '/std:c++17', '/EHsc', '/I' + str(ROOT / 'src/app'),
             str(source), str(ROOT / 'src/app/app_setup_handoff.cpp'), '/Fe:' + str(binary),
             '/link', '/MANIFEST:EMBED', '/MANIFESTINPUT:' + str(ROOT / 'src/app/app.manifest')]
            if os.name == 'nt' else
            [compiler, '-std=c++17', '-I' + str(ROOT / 'src/app'), str(source),
             str(ROOT / 'src/app/app_setup_handoff.cpp'), '-o', str(binary)])
    result = subprocess.run(args, cwd=folder, capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    return binary


def wait_report(report, process):
    deadline = time.monotonic() + 12
    while time.monotonic() < deadline:
        if report.exists() and report.stat().st_size:
            return report.read_text()
        time.sleep(.05)
    raise AssertionError(f'No handoff report; wrapper exit={process.poll()}')


@pytest.mark.parametrize('mode', ['normal', 'timeout', 'foreign', 'spawnfail'])
def test_waiting_legacy_wrapper_releases_before_replacement(native, tmp_path, mode):
    if os.name != 'nt':
        report = tmp_path / 'report'
        subprocess.run([str(native), 'app', str(tmp_path), str(report), mode], check=True)
        assert report.read_text() == 'unneeded'
        return
    tmp_path = tmp_path / 'space & ü'
    tmp_path.mkdir()
    wrapper = tmp_path / 'DefJamLauncher.exe'
    shutil.copyfile(native, tmp_path / 'fixture.exe')
    shutil.copyfile(native, wrapper)
    shutil.copyfile(native, tmp_path / 'DefJamLauncher.exe.new')
    root = tmp_path if mode != 'foreign' else tmp_path / 'other-install'
    report = tmp_path / 'report'
    child = subprocess.Popen([str(wrapper), 'legacy', str(root), str(report), mode],
                             creationflags=subprocess.CREATE_NO_WINDOW)
    try:
        message = wait_report(report, child)
        if mode == 'timeout':
            assert child.poll() is None, 'a timed-out wrapper was terminated'
        child.wait(timeout=12)
        assert child.returncode == 0
        if mode == 'normal':
            assert message == 'replaced'
            assert not (tmp_path / 'DefJamLauncher.exe.new').exists()
        elif mode == 'timeout':
            assert 'old launcher did not close' in message
            assert (tmp_path / 'DefJamLauncher.exe.new').exists()
        elif mode == 'foreign':
            assert message == 'unneeded'
        else:
            assert 'Could not restart for setup' in message
    finally:
        if child.poll() is None:
            child.kill()
            child.wait()


@pytest.mark.skipif(os.name != 'nt', reason='Windows mapped-executable semantics')
def test_running_launcher_preflight_and_unchanged_recovery(native, tmp_path):
    wrapper = tmp_path / 'DefJamLauncher.exe'
    shutil.copyfile(native, wrapper)
    report = tmp_path / 'ready'
    child = subprocess.Popen([str(wrapper), 'hold', str(tmp_path), str(report)],
                             creationflags=subprocess.CREATE_NO_WINDOW)
    try:
        assert wait_report(report, child) == 'holding'
        with pytest.raises(engine.SetupError, match='DefJamLauncher.exe') as failure:
            engine.check_launcher_closed(tmp_path)
        assert failure.value.code == 5
        data = tmp_path / 'data'
        data.mkdir()
        tx = engine.transaction.Activation(tmp_path, data)
        tx.begin([wrapper])
        pending = tmp_path / 'DefJamLauncher.exe.new'
        shutil.copyfile(native, pending)
        with pytest.raises(PermissionError):
            os.replace(pending, wrapper)
        tx.recover()  # The failed promotion left the mapped launcher unchanged.
        tx.recover()
        assert not tx.folder.exists()
    finally:
        child.terminate()
        child.wait(timeout=5)
    engine.check_launcher_closed(tmp_path)
