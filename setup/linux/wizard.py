"""Def Jam Setup for Linux: the wizard, and the entry point for silent runs.

    DefJamSetup.sh                                   # the wizard
    DefJamSetup.sh --silent --dump D --install-dir I --data-dir A [engine options]
    DefJamSetup.sh --uninstall --install-dir I

The wizard collects the same choices as the Windows and macOS ones and runs the
setup engine (engine.py, next to this file) as a child process, reading its
progress from a status file and stopping it through a cancel file. Anything
with --silent or --uninstall goes straight to the engine instead. Nothing here
reads the dump; the engine verifies it and builds it on this computer.
"""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

HERE = Path(__file__).resolve().parent
TITLE = 'Def Jam Recompiled Setup'


def state_home():
    return Path(os.environ.get('XDG_STATE_HOME') or Path.home() / '.local/state')


def default_locations():
    root = Path.home() / 'Games' / 'DefJamRecompiled'
    return root / 'App', root / 'Data'


def run_engine(argv):
    sys.path.insert(0, str(HERE))
    import engine
    return engine.main(argv)


def native_pick(kind, start):
    """A KDE or GNOME file chooser when there is one (Tk's own is hard to use on a Deck)."""
    start = str(start if Path(start).exists() else Path.home())
    if shutil.which('kdialog'):
        argv = {'file': ['kdialog', '--getopenfilename', start, 'Xbox disc images (*.iso *.xiso)'],
                'folder': ['kdialog', '--getexistingdirectory', start]}[kind]
    elif shutil.which('zenity'):
        argv = ['zenity', '--file-selection', '--filename=' + start + '/'] + (['--directory'] if kind == 'folder' else [])
    else:
        return None
    try:
        answer = subprocess.run(argv, capture_output=True, text=True)
    except OSError:
        return None
    return answer.stdout.strip() if answer.returncode == 0 else ''


def wizard(payload, smoke=False):
    import tkinter as tk
    from tkinter import filedialog, messagebox, ttk

    root = tk.Tk()
    root.title(TITLE)
    root.minsize(720, 0)
    try:
        root.tk.call('tk', 'scaling', max(1.0, root.winfo_fpixels('1i') / 72.0))
    except tk.TclError:
        pass
    install_default, data_default = default_locations()
    dump = tk.StringVar()
    install = tk.StringVar(value=str(install_default))
    data = tk.StringVar(value=str(data_default))
    shortcuts = tk.BooleanVar(value=True)
    desktop = tk.BooleanVar(value=True)
    steam = tk.BooleanVar(value=bool(shutil.which('steam')))
    status = tk.StringVar(value='Choose your Def Jam: Fight for NY (USA, Xbox) disc image or extracted folder.')
    session = {'child': None, 'cancel': None, 'status': None, 'log': None}

    frame = ttk.Frame(root, padding=16)
    frame.grid(sticky='nsew')
    root.columnconfigure(0, weight=1)
    frame.columnconfigure(1, weight=1)

    def browse(variable, kind):
        start = variable.get() or str(Path.home())
        picked = native_pick(kind, start)
        if picked is None:
            picked = (filedialog.askopenfilename(initialdir=start, filetypes=[('Xbox disc images', '*.iso *.xiso'),
                                                                              ('All files', '*')])
                      if kind == 'file' else filedialog.askdirectory(initialdir=start))
        if picked:
            variable.set(picked)

    def row(r, label, variable, kinds):
        ttk.Label(frame, text=label).grid(row=r, column=0, sticky='w', pady=4)
        ttk.Entry(frame, textvariable=variable).grid(row=r, column=1, sticky='ew', padx=8)
        for i, (text, kind) in enumerate(kinds):
            ttk.Button(frame, text=text, command=lambda k=kind: browse(variable, k)).grid(row=r, column=2 + i)

    row(0, 'Your game copy', dump, [('Disc image…', 'file'), ('Folder…', 'folder')])
    row(1, 'Install location', install, [('Choose…', 'folder')])
    row(2, 'Data location', data, [('Choose…', 'folder')])
    ttk.Label(frame, text='The data location keeps a copy of your game files, your saves and settings; '
                          'updates and uninstalling leave it alone.', wraplength=640,
              foreground='#555').grid(row=3, column=0, columnspan=4, sticky='w', pady=(0, 8))
    options = ttk.Frame(frame)
    options.grid(row=4, column=0, columnspan=4, sticky='w')
    ttk.Checkbutton(options, text='Add to the application menu', variable=shortcuts).grid(row=0, column=0, sticky='w')
    ttk.Checkbutton(options, text='Create a desktop shortcut', variable=desktop).grid(row=1, column=0, sticky='w')
    ttk.Checkbutton(options, text='Add to Steam (for Gaming Mode; Steam must be running)',
                    variable=steam).grid(row=2, column=0, sticky='w')
    ttk.Label(frame, text='Setup builds the game from your copy on this computer. The first install takes '
                          'a while; without a compiler it downloads a build container once (about 1 GB).',
              wraplength=640, foreground='#555').grid(row=5, column=0, columnspan=4, sticky='w', pady=8)
    progress = ttk.Progressbar(frame, mode='indeterminate')
    progress.grid(row=6, column=0, columnspan=4, sticky='ew', pady=4)
    ttk.Label(frame, textvariable=status, wraplength=640).grid(row=7, column=0, columnspan=4, sticky='w')
    buttons = ttk.Frame(frame)
    buttons.grid(row=8, column=0, columnspan=4, sticky='e', pady=(12, 0))
    install_button = ttk.Button(buttons, text='Install / Repair')
    cancel_button = ttk.Button(buttons, text='Cancel', state='disabled')
    logs_button = ttk.Button(buttons, text='Open logs', state='disabled')
    play_button = ttk.Button(buttons, text='Play', state='disabled')
    for i, button in enumerate((logs_button, cancel_button, install_button, play_button)):
        button.grid(row=0, column=i, padx=4)

    def launcher():
        return Path(install.get()).expanduser() / 'Def Jam Recompiled'

    def refresh_play():
        play_button.configure(state='normal' if launcher().is_file() and session['child'] is None else 'disabled')

    def start():
        if not dump.get():
            messagebox.showwarning(TITLE, 'Choose your disc image or extracted game folder first.')
            return
        logs = state_home() / 'DefJamSetup' / 'logs'
        logs.mkdir(parents=True, exist_ok=True)
        session['log'] = logs / time.strftime('setup-%Y%m%d-%H%M%S.log')
        work = Path(tempfile.mkdtemp(prefix='defjam-setup-'))
        session['status'], session['cancel'] = work / 'status.txt', work / 'cancel'
        argv = [sys.executable, '-I', '-B', str(HERE / 'wizard.py'), '--silent', '--payload', str(payload),
                '--dump', str(Path(dump.get()).expanduser()), '--install-dir', str(Path(install.get()).expanduser()),
                '--data-dir', str(Path(data.get()).expanduser()), '--log', str(session['log']),
                '--status-file', str(session['status']), '--cancel-file', str(session['cancel'])]
        if not shortcuts.get():
            argv.append('--no-shortcuts')
        elif not desktop.get():
            argv.append('--no-desktop-shortcut')
        if steam.get():
            argv.append('--steam-shortcut')
        session['child'] = subprocess.Popen(argv, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                            stderr=subprocess.DEVNULL, start_new_session=True)
        for button in (install_button, play_button):
            button.configure(state='disabled')
        cancel_button.configure(state='normal')
        logs_button.configure(state='normal')
        progress.start(12)
        status.set('Starting setup…')
        root.after(500, poll)

    def poll():
        child = session['child']
        with_status = session['status']
        if with_status and with_status.is_file():
            text = with_status.read_text(encoding='utf-8', errors='replace').strip()
            if text:
                status.set(text)
        if child.poll() is None:
            root.after(500, poll)
            return
        progress.stop()
        session['child'] = None
        cancel_button.configure(state='disabled')
        install_button.configure(state='normal')
        refresh_play()
        code = child.returncode
        if code == 0:
            status.set('Setup complete. Select Play, or start Def Jam Recompiled from the menu, the desktop or Steam.')
        else:
            messagebox.showerror(TITLE, status.get() + '\n\nThe setup log is ' + str(session['log']))

    def cancel():
        if session['cancel']:
            session['cancel'].touch()
            status.set('Cancelling…')

    def open_logs():
        if session['log'] and session['log'].exists():
            subprocess.Popen(['xdg-open', str(session['log'])], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    def play():
        subprocess.Popen([str(launcher())], stdin=subprocess.DEVNULL, start_new_session=True)

    def closing():
        if session['child'] is not None:
            if not messagebox.askyesno(TITLE, 'Setup is still running. Cancel it and quit?'):
                return
            cancel()
            try:
                session['child'].wait(timeout=30)
            except subprocess.TimeoutExpired:
                pass
        root.destroy()

    install_button.configure(command=start)
    cancel_button.configure(command=cancel)
    logs_button.configure(command=open_logs)
    play_button.configure(command=play)
    install.trace_add('write', lambda *_: refresh_play())
    refresh_play()
    root.protocol('WM_DELETE_WINDOW', closing)
    if smoke:
        root.after(500, root.destroy)   # --ui-smoke: build the window, show it, close it
    root.mainloop()
    return 0


def main(argv):
    if '--silent' in argv or '--uninstall' in argv:
        return run_engine(argv)
    if '--payload' not in argv:
        print('usage: DefJamSetup.sh [--silent ...]', file=sys.stderr)
        return 2
    payload = Path(argv[argv.index('--payload') + 1])
    if not os.environ.get('DISPLAY') and not os.environ.get('WAYLAND_DISPLAY'):
        print('No display: run with --silent (see READ ME FIRST.txt).', file=sys.stderr)
        return 2
    return wizard(payload, smoke='--ui-smoke' in argv)


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
