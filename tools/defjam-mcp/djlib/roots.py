"""Disposable data roots, one per test copy.

A root is <parent of data>\\data-dj-<owner>-<name>: junctions to <data>\\extracted, online-art and
override (read only: the game never writes there), a copy of the small save files (never the cache
partition images, 5 GB, unless asked; the runtime makes sparse new ones) and, optionally, a save
fixture copied over them (a folder of save files, or one with a `save` folder in it). A marker file
(.dj-root) says who made it; destroy() refuses anything without one, takes the junctions out with
`cmd /c rmdir` first (removing the link only) and checks the dump is still there before it deletes
the rest. Nothing is ever written into <data> itself.
"""
import datetime
import json
import os
import shutil
import subprocess

from . import machine

MARKER = ".dj-root"
LINKED = ("extracted", "online-art", "override")
PARTITIONS = tuple("Partition%d.img" % i for i in range(1, 6))


def parent():
    return os.path.dirname(machine.DATA.rstrip("\\/"))


def owner_tag():
    a = machine.agent_id()
    return a[-8:] if a.startswith("agent-") else a[:16]


def root_path(name):
    return os.path.join(parent(), "data-dj-%s-%s" % (owner_tag(), name))


def is_link(path):
    """A junction or a symbolic link (never to be recursed into)."""
    try:
        if os.path.islink(path):
            return True
        return bool(getattr(os.path, "isjunction", lambda p: False)(path))
    except OSError:
        return False


def make_junction(link, target):
    if machine.WINDOWS:
        r = subprocess.run(["cmd", "/c", "mklink", "/J", link, target], capture_output=True)
        if r.returncode or not os.path.isdir(link):
            raise OSError("mklink /J %s failed: %s" % (link, (r.stdout + r.stderr).decode("utf-8", "replace").strip()))
    else:
        os.symlink(target, link)


def remove_link(link):
    if machine.WINDOWS and os.path.isdir(link):
        subprocess.run(["cmd", "/c", "rmdir", link], capture_output=True)
    else:
        os.unlink(link)
    if os.path.lexists(link):
        raise OSError("could not remove the link " + link)


def _copy_save(src, dst, partitions):
    def ignore(d, names):
        return [n for n in names if not partitions and n in PARTITIONS]
    shutil.copytree(src, dst, ignore=ignore, dirs_exist_ok=True)


def create(name, fixture=None, partitions=False, data=None):
    """Make (or remake) the managed root `name`. Returns its path."""
    data = data or machine.DATA
    root = root_path(name)
    if os.path.exists(root):
        destroy(root)
    if not os.path.isfile(os.path.join(data, "extracted", "default.xbe")):
        raise FileNotFoundError("no dump at %s\\extracted (default.xbe)" % data)
    os.makedirs(root)
    with open(os.path.join(root, MARKER), "w", encoding="utf-8") as f:
        json.dump({"owner": machine.agent_id(), "pid": os.getpid(), "data": data,
                   "created": datetime.datetime.now().isoformat(timespec="seconds")}, f)
    for n in LINKED:
        if os.path.isdir(os.path.join(data, n)):
            make_junction(os.path.join(root, n), os.path.join(data, n))
    save = os.path.join(root, "save")
    if os.path.isdir(os.path.join(data, "save")):
        _copy_save(os.path.join(data, "save"), save, partitions)
    else:
        os.makedirs(save)
    if fixture:
        src = os.path.join(fixture, "save") if os.path.isdir(os.path.join(fixture, "save")) else fixture
        if not os.path.isdir(src):
            raise FileNotFoundError("save fixture not found: " + fixture)
        shutil.copytree(src, save, dirs_exist_ok=True)
    return root


def _links_under(root):
    """Every junction/symlink below root, without entering any of them."""
    out = []
    for cur, dirs, files in os.walk(root, topdown=True, followlinks=False):
        links = [d for d in dirs if is_link(os.path.join(cur, d))]
        out += [os.path.join(cur, d) for d in links]
        dirs[:] = [d for d in dirs if d not in links]
        out += [os.path.join(cur, f) for f in files if is_link(os.path.join(cur, f))]
    return out


def destroy(root, data=None):
    """Delete a managed root: links first (rmdir), then a check that the dump survived, then the rest."""
    data = data or machine.DATA
    root = os.path.abspath(root)
    if not os.path.isfile(os.path.join(root, MARKER)):
        raise PermissionError("not a managed data root (no %s): %s" % (MARKER, root))
    if os.path.normcase(root) == os.path.normcase(os.path.abspath(data)) or machine.inside(data, root):
        raise PermissionError("refusing to delete the data folder itself")
    for n in LINKED:
        p = os.path.join(root, n)
        if os.path.lexists(p) and is_link(p):
            remove_link(p)
    for p in _links_under(root):
        remove_link(p)
    if not os.path.isfile(os.path.join(data, "extracted", "default.xbe")):
        raise RuntimeError("the dump at %s\\extracted is missing after removing the links; root kept: %s" % (data, root))
    shutil.rmtree(root)
    return root


def listing():
    """Managed roots of this owner: [(path, marker dict)]."""
    out = []
    pre = "data-dj-%s-" % owner_tag()
    for n in sorted(os.listdir(parent())):
        p = os.path.join(parent(), n)
        if n.startswith(pre) and os.path.isfile(os.path.join(p, MARKER)):
            try:
                with open(os.path.join(p, MARKER), encoding="utf-8") as f:
                    out.append((p, json.load(f)))
            except (OSError, ValueError):
                out.append((p, {}))
    return out
