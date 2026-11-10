# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 sweeax-exe (FFNY Online)
# Ported from FFNY Studio (ffnystudio/cpparts.py) for the player-side install.
"""The created-player parts list (``createplayer_parts.xml``) and the prefab files (``cp_def0..3.xml``).

The parts list is one ``<partslist>`` with a section per part type, each introduced by a processing instruction
such as ``<?TypeBody = "Ubody"?>`` and holding ``<head>`` and ``<parts><contents>``::

    <contents>
        <count>94</count>                         number of <model> elements
        <model>
            <name>V2CP_111_0001_C</name>          the part's geometry entry (``.o`` in a v2cp archive)
            <count>3</count>                      number of <texture> elements of this model
            <texture>
                <name>V2CP_111_0001_A01_01</name> a texture entry (``.xsh``)
                <count>4</count>                  number of <colour> elements (absent when there are none)
                <colour><name>V2CP_111_0001_A01_02</name></colour>
                ...

The game reads it with a streaming text parser that trusts the counts, and a fighter selects a part by
(model, texture, colour) index per type, so this module edits the text in place and only ever **appends**:
a new texture goes after the model's last texture, a new model after the section's last model, and every
count on the way is updated. Line endings and the tab indentation of the original are kept.

The prefab files (``cp_def%d.xml``) hold one ``<parts>`` block per type with ``<model>``, ``<texture>`` and
``<colour>`` indices (-1 for none); :func:`set_selection` changes one of them.
"""

import re
import xml.etree.ElementTree as ET

TYPE_PI = re.compile(r'<\?Type\w*\s*=\s*"([^"]+)"[^?]*\?>')
MAX_NAME = 31   # the game copies names into 32-byte stack buffers without a bound


class PartsError(ValueError):
    pass


def _newline(text):
    return "\r\n" if "\r\n" in text else "\n"


def sections(text):
    """[(type name, start, end)] of every section: start at its ``<?Type...?>``, end after its ``</parts>``."""
    marks = [(m.group(1), m.start()) for m in TYPE_PI.finditer(text)]
    out = []
    for i, (name, start) in enumerate(marks):
        limit = marks[i + 1][1] if i + 1 < len(marks) else len(text)
        close = text.rfind("</parts>", start, limit)
        if close < 0:
            raise PartsError("section %s has no </parts>" % name)
        out.append((name, start, close + len("</parts>")))
    return out


def section_span(text, type_name, occurrence=0):
    found = [s for s in sections(text) if s[0].lower() == type_name.lower()]
    if len(found) <= occurrence:
        raise PartsError("no section %r" % type_name)
    return found[occurrence][1:]


def _contents(text, span):
    start, end = span
    body = text[start:end]
    return ET.fromstring(body[body.index("<parts>"):])


def summary(text):
    """[(type name, models, textures, colours)] for every section, counted from the elements."""
    out = []
    for name, start, end in sections(text):
        contents = _contents(text, (start, end)).find("contents")
        models = contents.findall("model") if contents is not None else []
        textures = [t for m in models for t in m.findall("texture")]
        out.append((name, len(models), len(textures), sum(len(t.findall("colour")) for t in textures)))
    return out


def check(text):
    """Every ``<count>`` that does not match the elements it counts: [(type, path, stated, actual)]."""
    problems = []

    def stated(element):
        c = element.find("count")
        return int(c.text) if c is not None else 0

    for name, start, end in sections(text):
        contents = _contents(text, (start, end)).find("contents")
        if contents is None:
            continue
        models = contents.findall("model")
        if stated(contents) != len(models):
            problems.append((name, "contents", stated(contents), len(models)))
        for m in models:
            mname = m.findtext("name")
            textures = m.findall("texture")
            if stated(m) != len(textures):
                problems.append((name, mname, stated(m), len(textures)))
            for t in textures:
                if stated(t) != len(t.findall("colour")):
                    problems.append((name, mname + "/" + t.findtext("name"), stated(t), len(t.findall("colour"))))
    return problems


def models(text, type_name):
    """[(model name, [(texture name, [colour names])])] of one section, in file order (= the game's indices)."""
    contents = _contents(text, section_span(text, type_name)).find("contents")
    return [(m.findtext("name"), [(t.findtext("name"), [c.findtext("name") for c in t.findall("colour")])
                                  for t in m.findall("texture")]) for m in contents.findall("model")]


def _check_name(name):
    if not re.fullmatch(r"[A-Za-z0-9_]{1,%d}" % MAX_NAME, name or ""):
        raise PartsError("part names are 1 to %d letters, digits or underscores: %r" % (MAX_NAME, name))


def _bump_count(text, pos, nl):
    """Increment the ``<count>`` that directly follows ``pos`` (the element's own count). Returns the new text
    and the length change."""
    m = re.compile(r"<count>(\d+)</count>").search(text, pos)
    if m is None:
        raise PartsError("missing <count>")
    new = "<count>%d</count>" % (int(m.group(1)) + 1)
    return text[:m.start()] + new + text[m.end():], len(new) - len(m.group(0))


def _texture_block(name, colours, indent, nl):
    t = "\t"
    lines = [indent + "<texture>", indent + t + "<name>%s</name>" % name]
    if colours:
        lines.append(indent + t + "<count>%d</count>" % len(colours))
        for c in colours:
            lines += [indent + t + "<colour>", indent + t * 2 + "<name>%s</name>" % c, indent + t + "</colour>"]
    lines.append(indent + "</texture>")
    return nl.join(lines) + nl


def _model_span(text, span, model_name):
    start, end = span
    pat = re.compile(r"<model>\s*<name>%s</name>" % re.escape(model_name))
    m = pat.search(text, start, end)
    if m is None:
        raise PartsError("no model %r in this section" % model_name)
    close = text.find("</model>", m.end(), end)
    return m.start(), close


def append_texture(text, type_name, model_name, texture_name, colours=()):
    """Add a texture (with optional colour variants) after the last texture of ``model_name``.

    Existing textures keep their indices; the new one gets the next index. Returns the new text."""
    for n in (texture_name,) + tuple(colours):
        _check_name(n)
    span = section_span(text, type_name)
    if any(texture_name == t or texture_name in c for _, ts in models(text, type_name) for t, c in ts):
        raise PartsError("texture %r is already listed" % texture_name)
    nl = _newline(text)
    m_start, m_close = _model_span(text, span, model_name)
    text, _ = _bump_count(text, text.index("</name>", m_start), nl)
    m_close = text.find("</model>", m_start)
    line_start = text.rfind(nl, 0, m_close) + len(nl)
    indent = text[line_start:m_close] + "\t"
    block = _texture_block(texture_name, list(colours), indent, nl)
    return text[:line_start] + block + text[line_start:]


def append_colour(text, type_name, model_name, texture_name, colour_name):
    """Add a colour variant (a texture entry the game finds by name) after the last colour of ``texture_name``.

    The shops name a colour's texture ``V2CP_<cat>_<num>_<group>_<Number>`` and the game finds it by scanning
    this list by name, so a new colour of an existing item needs its name here. Returns the new text."""
    _check_name(colour_name)
    span = section_span(text, type_name)
    if any(colour_name == t or colour_name in c for _, ts in models(text, type_name) for t, c in ts):
        raise PartsError("texture %r is already listed" % colour_name)
    nl = _newline(text)
    m_start, m_close = _model_span(text, span, model_name)
    tm = re.compile(r"<texture>\s*<name>%s</name>" % re.escape(texture_name)).search(text, m_start, m_close)
    if tm is None:
        raise PartsError("no texture %r in model %r" % (texture_name, model_name))
    t_close = text.find("</texture>", tm.end())
    name_end = text.index("</name>", tm.start()) + len("</name>")
    line_start = text.rfind(nl, 0, t_close) + len(nl)
    indent = text[line_start:t_close] + "	"
    count = re.compile(r"\s*<count>(\d+)</count>").match(text, name_end)
    block = nl.join([indent + "<colour>", indent + "	<name>%s</name>" % colour_name, indent + "</colour>"]) + nl
    if count is None:
        text = text[:name_end] + nl + indent + "<count>1</count>" + text[name_end:]
        t_close = text.find("</texture>", tm.end())
        line_start = text.rfind(nl, 0, t_close) + len(nl)
    else:
        new = "<count>%d</count>" % (int(count.group(1)) + 1)
        a = text.index("<count>", name_end)
        b = text.index("</count>", a) + len("</count>")
        text = text[:a] + new + text[b:]
        t_close = text.find("</texture>", tm.end())
        line_start = text.rfind(nl, 0, t_close) + len(nl)
    return text[:line_start] + block + text[line_start:]


def append_model(text, type_name, model_name, textures=()):
    """Add a model after the last model of the section. ``textures`` is [(texture name, [colour names])].

    Existing models keep their indices; the new one gets the next index. Returns the new text."""
    _check_name(model_name)
    for t, cs in textures:
        for n in (t,) + tuple(cs):
            _check_name(n)
    span = section_span(text, type_name)
    if any(model_name == m for m, _ in models(text, type_name)):
        raise PartsError("model %r is already listed" % model_name)
    nl = _newline(text)
    start, end = span
    contents = text.index("<contents>", start, end)
    text, delta = _bump_count(text, contents, nl)
    end += delta
    close = text.rfind("</contents>", start, end)
    line_start = text.rfind(nl, 0, close) + len(nl)
    indent = text[line_start:close] + "\t"
    t = "\t"
    lines = [indent + "<model>", indent + t + "<name>%s</name>" % model_name,
             indent + t + "<count>%d</count>" % len(textures)]
    block = nl.join(lines) + nl
    for name, colours in textures:
        block += _texture_block(name, list(colours), indent + t, nl)
    block += indent + "</model>" + nl
    return text[:line_start] + block + text[line_start:]


def set_selection(text, type_name, model=None, texture=None, colour=None):
    """In a prefab file (``cp_def%d.xml``): set the model, texture and/or colour index of one part type."""
    m = re.search(r'<\?Type\s*=\s*"%s"\?>' % re.escape(type_name), text)
    if m is None:
        raise PartsError("no part type %r in this prefab" % type_name)
    end = text.index("</parts>", m.end())
    block = text[m.end():end]
    for tag, value in (("model", model), ("texture", texture), ("colour", colour)):
        if value is None:
            continue
        block, n = re.subn(r"<%s>-?\d+</%s>" % (tag, tag), "<%s>%d</%s>" % (tag, int(value), tag), block, count=1)
        if not n:
            raise PartsError("no <%s> for %s" % (tag, type_name))
    return text[:m.end()] + block + text[end:]
