"""The ranked emblems: the port's own art (the owner's approved design "Ranks", 2026-11-03), as SVG.

SVG <defs> (gradients, filters, symbols). Large symbols em-1..em-8 use a 256 box (art inside y 8..218,
chevrons below); small symbols sm-1..sm-8 use a 64 box (the 64 px texture, shown down to 24 px).
scripts/rank-emblems.py renders them to PNG on the player's computer (no image is in the repository);
the game draws the division chevrons and the Legend plate itself (ranked_ui.inc)."""
import math

TIERS = [
    # key, name, material, colour, glow, floor, divs
    dict(n=1, name="STREET ROOKIE", mat="Concrete and spray paint", col="#5FDB8F", glow="95,219,143", floor=None, rng="below 1200", divs=("III", "below 1100"), d=["below 1100", "1100", "1150"]),
    dict(n=2, name="HUSTLER", mat="Raw steel", col="#8DB8E8", glow="141,184,232", rng="1200 to 1349", d=["1200", "1250", "1300"]),
    dict(n=3, name="BRAWLER", mat="Gunmetal and tape", col="#F0584A", glow="240,88,74", rng="1350 to 1499", d=["1350", "1400", "1450"]),
    dict(n=4, name="CONTENDER", mat="Bronze", col="#E39A5C", glow="227,154,92", rng="1500 to 1649", d=["1500", "1550", "1600"]),
    dict(n=5, name="ENFORCER", mat="Silver", col="#D4E2F4", glow="212,226,244", rng="1650 to 1799", d=["1650", "1700", "1750"]),
    dict(n=6, name="KINGPIN", mat="Gold", col="#FFC53D", glow="255,197,61", rng="1800 to 1949", d=["1800", "1850", "1900"]),
    dict(n=7, name="DON OF THE BRONX", mat="Platinum and diamond", col="#8FEBFF", glow="143,235,255", rng="1950 to 2099", d=["1950", "2000", "2050"]),
    dict(n=8, name="DEF JAM LEGEND", mat="Diamond and neon fire", col="#FF4D5E", glow="255,77,94", rng="2100 and up", d=[]),
]

# metal ramps: name -> stops (offset, colour). Forward = light top, reverse id = name+'R'
METALS = {
    "conc":   [(0, "#B7B2A9"), (.5, "#8A857D"), (1, "#5E5A54")],
    "steel":  [(0, "#FFFFFF"), (.38, "#B9C4CF"), (.5, "#4C5763"), (.56, "#6E7A87"), (.82, "#C9D2DB"), (1, "#7C8894")],
    "gun":    [(0, "#B4C7DB"), (.4, "#55697F"), (.5, "#202A36"), (.62, "#3E5064"), (.9, "#7F97B0"), (1, "#3A4858")],
    "bronze": [(0, "#FFE2BF"), (.32, "#E19A5C"), (.5, "#7A3A15"), (.6, "#B4642D"), (.86, "#F0B07A"), (1, "#6E3010")],
    "silver": [(0, "#FFFFFF"), (.4, "#DCE3EA"), (.5, "#7E8995"), (.58, "#BCC6D0"), (.85, "#FFFFFF"), (1, "#9AA5B1")],
    "gold":   [(0, "#FFF7D1"), (.3, "#FFD45A"), (.5, "#A8650A"), (.58, "#E8A124"), (.84, "#FFE58A"), (1, "#8C5006")],
    "plat":   [(0, "#FFFFFF"), (.36, "#D8F6FF"), (.5, "#6FA9C4"), (.6, "#B8E9FA"), (.86, "#F4FDFF"), (1, "#8EB8CB")],
    "tape":   [(0, "#FFFFFF"), (.6, "#EDE6D5"), (1, "#B9AF98")],
    "ice":    [(0, "#FFFFFF"), (.45, "#C9F5FF"), (1, "#53B9E0")],
    "fireO":  [(0, "#FFE07A"), (.45, "#FFA81A"), (1, "#FF2E4D")],
}
TIER_METAL = {1: "conc", 2: "steel", 3: "gun", 4: "bronze", 5: "silver", 6: "gold", 7: "plat", 8: "gold"}

O = "#060507"  # outline ink


def grads():
    out = []
    for k, stops in METALS.items():
        s = "".join(f'<stop offset="{o}" stop-color="{c}"/>' for o, c in stops)
        out.append(f'<linearGradient id="m-{k}" x1="0" y1="0" x2="0.18" y2="1">{s}</linearGradient>')
        out.append(f'<linearGradient id="m-{k}R" x1="0.18" y1="1" x2="0" y2="0">{s}</linearGradient>')
        out.append(f'<linearGradient id="m-{k}H" x1="0" y1="0" x2="1" y2="0.25">{s}</linearGradient>')
    out.append('''
<radialGradient id="r-face-steel" cx="0.38" cy="0.3" r="0.8"><stop offset="0" stop-color="#5D6874"/><stop offset="0.6" stop-color="#2A313A"/><stop offset="1" stop-color="#14181D"/></radialGradient>
<radialGradient id="r-face-gun" cx="0.4" cy="0.25" r="0.85"><stop offset="0" stop-color="#2E3B4A"/><stop offset="1" stop-color="#0C1118"/></radialGradient>
<radialGradient id="r-face-red" cx="0.4" cy="0.3" r="0.8"><stop offset="0" stop-color="#8E1622"/><stop offset="0.7" stop-color="#4A0910"/><stop offset="1" stop-color="#240407"/></radialGradient>
<radialGradient id="r-face-navy" cx="0.4" cy="0.25" r="0.85"><stop offset="0" stop-color="#1F3561"/><stop offset="1" stop-color="#070F20"/></radialGradient>
<radialGradient id="r-face-black" cx="0.4" cy="0.3" r="0.8"><stop offset="0" stop-color="#2A2210"/><stop offset="1" stop-color="#090703"/></radialGradient>
<radialGradient id="r-face-night" cx="0.5" cy="0.15" r="0.9"><stop offset="0" stop-color="#3A2A78"/><stop offset="0.55" stop-color="#170D36"/><stop offset="1" stop-color="#07040F"/></radialGradient>
<radialGradient id="r-obsidian" cx="0.45" cy="0.3" r="0.8"><stop offset="0" stop-color="#3B1018"/><stop offset="1" stop-color="#0A0306"/></radialGradient>
<linearGradient id="l-sign" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#1A9A58"/><stop offset="1" stop-color="#0B5E33"/></linearGradient>
<linearGradient id="l-signB" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#0E6B3B"/><stop offset="1" stop-color="#063A1F"/></linearGradient>
<linearGradient id="l-ivory" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#FFFFFF"/><stop offset="1" stop-color="#D8D0BF"/></linearGradient>
<linearGradient id="l-sky" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#BFF3FF"/><stop offset="1" stop-color="#3C8FB8"/></linearGradient>
<linearGradient id="l-neon" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#FFE07A"/><stop offset="0.5" stop-color="#FF3B54"/><stop offset="1" stop-color="#FF2E9A"/></linearGradient>
<radialGradient id="aura-gold"><stop offset="0" stop-color="#FFC53D" stop-opacity="0.55"/><stop offset="1" stop-color="#FFC53D" stop-opacity="0"/></radialGradient>
<radialGradient id="aura-ice"><stop offset="0" stop-color="#8FEBFF" stop-opacity="0.6"/><stop offset="1" stop-color="#8FEBFF" stop-opacity="0"/></radialGradient>
<radialGradient id="aura-fire"><stop offset="0" stop-color="#FF7A2E" stop-opacity="0.85"/><stop offset="0.5" stop-color="#FF2E4D" stop-opacity="0.35"/><stop offset="1" stop-color="#FF2E4D" stop-opacity="0"/></radialGradient>
<pattern id="p-brick" width="32" height="16" patternUnits="userSpaceOnUse"><path d="M0 0.5H32M0 8.5H32M8 0V8M24 8V16" stroke="#2B2825" stroke-width="2"/></pattern>
<pattern id="p-brush" width="256" height="3" patternUnits="userSpaceOnUse"><path d="M0 1H256" stroke="#FFFFFF" stroke-opacity="0.06" stroke-width="1"/></pattern>
<filter id="f-rough" x="-5%" y="-5%" width="110%" height="110%"><feTurbulence type="fractalNoise" baseFrequency="0.09" numOctaves="2" seed="4"/><feDisplacementMap in="SourceGraphic" scale="7"/></filter>
<filter id="f-noise" x="0" y="0" width="100%" height="100%"><feTurbulence type="fractalNoise" baseFrequency="0.9" numOctaves="2" seed="9"/><feColorMatrix values="0 0 0 0 0  0 0 0 0 0  0 0 0 0 0  0 0 0 -1.4 0.75"/><feComposite in2="SourceGraphic" operator="in"/></filter>
<filter id="f-spray" x="-20%" y="-20%" width="140%" height="140%"><feGaussianBlur stdDeviation="1.6"/></filter>
<filter id="f-glow" x="-50%" y="-50%" width="200%" height="200%"><feGaussianBlur stdDeviation="5"/></filter>
<filter id="f-glow2" x="-50%" y="-50%" width="200%" height="200%"><feGaussianBlur stdDeviation="2.2"/></filter>
<filter id="f-drop" x="-20%" y="-20%" width="140%" height="150%"><feDropShadow dx="0" dy="6" stdDeviation="6" flood-color="#000" flood-opacity="0.75"/></filter>
''')
    return "\n".join(out)


def sym(i, body, box=256, pre="em"):
    return f'<symbol id="{pre}-{i}" viewBox="0 0 {box} {box}" overflow="visible">{body}</symbol>'


def scaled(href, s, cx=128, cy=128, extra=""):
    return f'<use href="#{href}" transform="translate({cx} {cy}) scale({s}) translate({-cx} {-cy})" {extra}/>'


def poly(pts):
    return " ".join(f"{x:.1f},{y:.1f}" for x, y in pts)


def star(cx, cy, R, r, n=5, rot=-90):
    pts = []
    for k in range(n * 2):
        a = math.radians(rot + k * 180 / n)
        rad = R if k % 2 == 0 else r
        pts.append((cx + rad * math.cos(a), cy + rad * math.sin(a)))
    return pts


def faceted_star(cx, cy, R, r, light, dark):
    pts = star(cx, cy, R, r)
    s = f'<polygon points="{poly(pts)}" fill="{light}" stroke="{O}" stroke-width="4" stroke-linejoin="round"/>'
    for k in range(0, 10):
        a, b = pts[k], pts[(k + 1) % 10]
        fill = dark if k % 2 == 0 else light
        s += f'<polygon points="{poly([(cx, cy), a, b])}" fill="{fill}"/>'
    s += f'<polygon points="{poly(pts)}" fill="none" stroke="{O}" stroke-width="2.5" stroke-linejoin="round"/>'
    return s


# ---------------------------------------------------------------- large emblems
def em1():  # STREET ROOKIE: bent street sign on sprayed brick
    slab = "M50 34 L204 30 L222 54 L218 198 L198 216 L58 220 L36 198 L38 58 Z"
    return f'''
<path id="e1slab" d="{slab}" fill="{O}" transform="translate(0 4)"/>
<g filter="url(#f-rough)"><path d="{slab}" fill="url(#m-conc)" stroke="{O}" stroke-width="5"/>
<path d="{slab}" fill="url(#p-brick)" opacity="0.55"/></g>
<path d="{slab}" fill="#000" filter="url(#f-noise)" opacity="0.6"/>
<g filter="url(#f-spray)" opacity="0.95">
 <circle cx="128" cy="128" r="70" fill="none" stroke="#3FD47F" stroke-width="15"/>
 <path d="M96 196V214M150 194V208M118 198V222M176 178V196" stroke="#3FD47F" stroke-width="6" stroke-linecap="round"/>
 <path d="M58 70l8 8M200 186l-8-6" stroke="#3FD47F" stroke-width="4" stroke-linecap="round"/>
</g>
<circle cx="128" cy="128" r="70" fill="none" stroke="#B8FFD6" stroke-width="3" opacity="0.45" stroke-dasharray="40 18 70 24"/>
<rect x="121" y="112" width="14" height="106" fill="url(#m-steelH)" stroke="{O}" stroke-width="4"/>
<path d="M121 150h14M121 186h14" stroke="{O}" stroke-width="2" opacity="0.6"/>
<path d="M34 80 L128 90 L128 128 L34 118 Z" fill="url(#l-sign)" stroke="{O}" stroke-width="5" stroke-linejoin="round"/>
<path d="M128 90 L220 116 L214 154 L128 128 Z" fill="url(#l-signB)" stroke="{O}" stroke-width="5" stroke-linejoin="round"/>
<path d="M40 86 L124 95 L124 123 L40 113 Z" fill="none" stroke="#EAF6EE" stroke-width="2.6"/>
<path d="M132 96 L214 119 L209 149 L132 124 Z" fill="none" stroke="#CFE3D6" stroke-width="2.6"/>
<text x="82" y="113" transform="rotate(6 82 104)" text-anchor="middle" font-family="Anton" font-size="23" fill="#F4FFF7" letter-spacing="1">ROOKIE</text>
<text x="171" y="132" transform="rotate(16 171 124)" text-anchor="middle" font-family="Anton" font-size="22" fill="#D7EBDD">ST</text>
<path d="M128 90 L128 128" stroke="#9FD9B6" stroke-width="2" opacity="0.7"/>
<circle cx="128" cy="98" r="3.2" fill="#C9D2DB" stroke="{O}" stroke-width="1.5"/><circle cx="128" cy="120" r="3.2" fill="#C9D2DB" stroke="{O}" stroke-width="1.5"/>
<path d="M40 84 L124 93" stroke="#FFFFFF" stroke-width="2" opacity="0.35"/>
'''


def em2():  # HUSTLER: dice in a steel chain ring
    links_d, links = "", ""
    n = 16
    for i in range(n):
        a = i * 360 / n
        t = f'transform="rotate({a:.2f} 128 120) translate(128 22)"'
        if i % 2 == 0:
            links_d += f'<rect x="-17" y="-9" width="34" height="18" rx="9" fill="none" stroke="{O}" stroke-width="11" {t}/>'
            links += f'<rect x="-17" y="-9" width="34" height="18" rx="9" fill="none" stroke="url(#m-steel)" stroke-width="6" {t}/>'
        else:
            links_d += f'<rect x="-18" y="-4.5" width="36" height="9" rx="4.5" fill="{O}" stroke="{O}" stroke-width="5" {t}/>'
            links += f'<rect x="-17" y="-3.5" width="34" height="7" rx="3.5" fill="url(#m-steel)" {t}/>'

    def die(cx, cy, s, rot, pips):
        h = s / 2
        g = f'<g transform="rotate({rot} {cx} {cy})">'
        g += f'<rect x="{cx-h+7}" y="{cy-h+7}" width="{s}" height="{s}" rx="11" fill="#7D7466" stroke="{O}" stroke-width="5"/>'
        g += f'<rect x="{cx-h}" y="{cy-h}" width="{s}" height="{s}" rx="11" fill="url(#l-ivory)" stroke="{O}" stroke-width="5"/>'
        g += f'<path d="M{cx-h+6} {cy-h+4}H{cx+h-10}" stroke="#fff" stroke-width="3" stroke-linecap="round"/>'
        q = s * 0.27
        spots = {3: [(-1, -1), (0, 0), (1, 1)], 4: [(-1, -1), (1, -1), (-1, 1), (1, 1)], 5: [(-1, -1), (1, -1), (0, 0), (-1, 1), (1, 1)]}[pips]
        for dx, dy in spots:
            g += f'<circle cx="{cx+dx*q:.1f}" cy="{cy+dy*q:.1f}" r="{s*0.085:.1f}" fill="#141016"/>'
        return g + "</g>"
    return f'''
<circle cx="128" cy="120" r="104" fill="{O}"/>
<circle cx="128" cy="120" r="98" fill="url(#r-face-steel)"/>
<circle cx="128" cy="120" r="98" fill="url(#p-brush)"/>
{links_d}{links}
<circle cx="128" cy="120" r="78" fill="url(#m-steelR)" stroke="{O}" stroke-width="5"/>
<circle cx="128" cy="120" r="70" fill="url(#r-face-steel)" stroke="{O}" stroke-width="3"/>
<circle cx="128" cy="120" r="70" fill="url(#p-brush)"/>
<circle cx="128" cy="120" r="58" fill="none" stroke="#8DB8E8" stroke-width="1.5" opacity="0.35" stroke-dasharray="3 5"/>
{die(102, 112, 56, -16, 4)}
{die(156, 134, 50, 14, 3)}
<path d="M54 74 A86 86 0 0 1 104 38" stroke="#fff" stroke-width="4" stroke-linecap="round" opacity="0.55" fill="none"/>
'''


HEX = [(128, 22), (213, 71), (213, 169), (128, 218), (43, 169), (43, 71)]


def em3():  # BRAWLER: taped fist on a riveted gunmetal hex
    hp = poly(HEX)
    rivets = ""
    for (x, y) in HEX:
        x2 = 128 + (x - 128) * 0.86
        y2 = 120 + (y - 120) * 0.86
        rivets += f'<circle cx="{x2:.1f}" cy="{y2:.1f}" r="5.5" fill="url(#m-steel)" stroke="{O}" stroke-width="2"/>'
    cracks = '<path d="M60 150 L82 138 L76 126 M196 92 L176 104 L186 116 M70 84 L90 96 M190 156 L170 146 L176 170" stroke="#9FC3E8" stroke-width="2" fill="none" opacity="0.5"/>'
    fingers = ""
    for k, x in enumerate([82, 106, 130, 154]):
        y = 64 + [6, 0, 2, 10][k]
        fingers += f'<rect x="{x}" y="{y}" width="24" height="46" rx="11" fill="url(#m-tape)" stroke="{O}" stroke-width="4.5"/>'
        fingers += f'<path d="M{x+5} {y+9}h13" stroke="#fff" stroke-width="3" stroke-linecap="round" opacity="0.9"/>'
        fingers += f'<path d="M{x+3} {y+26}l18 -6M{x+3} {y+36}l18 -6" stroke="#B4A88E" stroke-width="1.6"/>'
    return f'''
<polygon points="{hp}" fill="{O}" transform="translate(0 4)"/>
<polygon points="{hp}" fill="url(#m-gun)" stroke="{O}" stroke-width="5" stroke-linejoin="round"/>
<g transform="translate(128 120) scale(0.86) translate(-128 -120)"><polygon points="{hp}" fill="url(#m-gunR)" stroke="{O}" stroke-width="3"/></g>
<g transform="translate(128 120) scale(0.76) translate(-128 -120)"><polygon points="{hp}" fill="url(#r-face-gun)" stroke="{O}" stroke-width="4"/></g>
{rivets}{cracks}
<g filter="url(#f-drop)">
 <rect x="98" y="150" width="62" height="64" rx="6" fill="url(#m-tape)" stroke="{O}" stroke-width="4.5"/>
 <path d="M100 164h58M100 178h58M100 192h58" stroke="#B4A88E" stroke-width="2"/>
 <rect x="98" y="196" width="62" height="10" fill="#D7262E" stroke="{O}" stroke-width="3"/>
 <rect x="80" y="96" width="100" height="70" rx="18" fill="url(#m-tape)" stroke="{O}" stroke-width="4.5"/>
 {fingers}
 <path d="M84 120 L150 108 Q164 106 166 118 Q168 130 154 134 L90 146 Q78 146 78 134 Q78 122 84 120 Z" fill="url(#m-tape)" stroke="{O}" stroke-width="4.5"/>
 <path d="M90 126 L150 115" stroke="#fff" stroke-width="3" stroke-linecap="round"/>
 <path d="M82 148 L178 128 L180 140 L84 160 Z" fill="#D7262E" stroke="{O}" stroke-width="3.5"/>
 <path d="M86 150 L176 131" stroke="#FF8A80" stroke-width="2"/>
</g>
'''


def em4():  # CONTENDER: bronze championship belt plate
    beads = ""
    for i in range(30):
        a = math.radians(i * 12)
        beads += f'<circle cx="{128+75*math.cos(a):.1f}" cy="{122+75*math.sin(a):.1f}" r="3.6" fill="url(#m-bronze)" stroke="{O}" stroke-width="1.4"/>'
    side = lambda x: f'''<rect x="{x}" y="92" width="44" height="60" rx="9" fill="url(#m-bronze)" stroke="{O}" stroke-width="4.5"/>
<rect x="{x+6}" y="98" width="32" height="48" rx="6" fill="url(#r-face-red)" stroke="{O}" stroke-width="2.5"/>
{faceted_star(x+22, 122, 12, 5, "#F4B57D", "#9C5222")}'''
    return f'''
<path d="M2 100 Q128 88 254 100 L254 146 Q128 158 2 146 Z" fill="#160C08" stroke="{O}" stroke-width="5"/>
<path d="M4 108 Q128 96 252 108M4 138 Q128 150 252 138" stroke="#A7703F" stroke-width="2" stroke-dasharray="5 5" fill="none"/>
<path d="M4 104 Q128 92 252 104" stroke="#fff" stroke-width="1.5" opacity="0.18" fill="none"/>
{side(8)}{side(204)}
<g filter="url(#f-drop)">
<path d="M128 22 L176 36 L208 72 L214 122 L204 170 L172 206 L128 222 L84 206 L52 170 L42 122 L48 72 L80 36 Z" fill="url(#m-bronze)" stroke="{O}" stroke-width="5" stroke-linejoin="round"/>
</g>
<circle cx="128" cy="122" r="84" fill="url(#m-bronzeR)" stroke="{O}" stroke-width="3"/>
{beads}
<circle cx="128" cy="122" r="66" fill="url(#m-bronze)" stroke="{O}" stroke-width="4"/>
<circle cx="128" cy="122" r="58" fill="url(#r-face-red)" stroke="{O}" stroke-width="3"/>
<path id="e4arc" d="M86 122 A42 42 0 0 1 170 122" fill="none"/>
<text font-family="Anton" font-size="13" letter-spacing="2.4" fill="#F0B07A"><textPath href="#e4arc" startOffset="50%" text-anchor="middle">CONTENDER</textPath></text>
{faceted_star(128, 132, 38, 16, "#FFD2A3", "#A65A26")}
<path d="M60 92 A74 74 0 0 1 112 52" stroke="#fff" stroke-width="3.5" stroke-linecap="round" opacity="0.6" fill="none"/>
'''


SHIELD = "M128 18 L216 46 L210 138 C204 180 172 204 128 222 C84 204 52 180 46 138 L40 46 Z"


def em5():  # ENFORCER: chrome brass-knuckles on a silver shield
    rings = ""
    for x in (83, 111, 139, 167):
        rings += f'<circle cx="{x}" cy="108" r="17" fill="#050A16" stroke="{O}" stroke-width="16"/>'
    for x in (83, 111, 139, 167):
        rings += f'<circle cx="{x}" cy="108" r="17" fill="none" stroke="url(#m-silver)" stroke-width="9"/>'
        rings += f'<path d="M{x-13} 99 A17 17 0 0 1 {x+4} 91" stroke="#fff" stroke-width="2.5" fill="none" stroke-linecap="round"/>'
    return f'''
<path d="{SHIELD}" fill="{O}" transform="translate(0 5)"/>
<path d="{SHIELD}" fill="url(#m-silver)" stroke="{O}" stroke-width="5" stroke-linejoin="round"/>
<g transform="translate(128 120) scale(0.88) translate(-128 -120)"><path d="{SHIELD}" fill="url(#m-silverR)" stroke="{O}" stroke-width="3"/></g>
<g transform="translate(128 120) scale(0.8) translate(-128 -120)"><path d="{SHIELD}" fill="url(#r-face-navy)" stroke="{O}" stroke-width="4"/></g>
<path d="M128 40 V200" stroke="#9FB6D8" stroke-width="1.5" opacity="0.25"/>
<path d="M70 176 Q128 206 186 176" stroke="#D4E2F4" stroke-width="3" fill="none" opacity="0.35"/>
<path d="M82 190 Q128 214 174 190" stroke="#D4E2F4" stroke-width="2" fill="none" opacity="0.2"/>
<circle cx="64" cy="54" r="6" fill="url(#m-silver)" stroke="{O}" stroke-width="2"/><circle cx="192" cy="54" r="6" fill="url(#m-silver)" stroke="{O}" stroke-width="2"/><circle cx="128" cy="34" r="6" fill="url(#m-silver)" stroke="{O}" stroke-width="2"/>
<g filter="url(#f-drop)">
{rings}
<path d="M62 124 Q128 114 194 124 L188 148 Q128 176 68 148 Z" fill="url(#m-silver)" stroke="{O}" stroke-width="5" stroke-linejoin="round"/>
<path d="M72 129 Q128 120 184 129" stroke="#fff" stroke-width="3" fill="none" stroke-linecap="round"/>
<path d="M84 152 Q128 168 172 152" stroke="#5A6672" stroke-width="2" fill="none"/><path d="M104 134v14M128 132v16M152 134v14" stroke="#5A6672" stroke-width="2"/>
</g>
<path d="M48 52 L126 26" stroke="#fff" stroke-width="3" opacity="0.7" stroke-linecap="round"/>
'''


def chain(x0, y0, x1, y1, n):
    s_d, s = "", ""
    ang = math.degrees(math.atan2(y1 - y0, x1 - x0))
    for i in range(n):
        t = (i + 0.5) / n
        x, y = x0 + (x1 - x0) * t, y0 + (y1 - y0) * t
        if i % 2 == 0:
            s_d += f'<ellipse cx="0" cy="0" rx="10" ry="6.5" fill="none" stroke="{O}" stroke-width="9" transform="translate({x:.1f} {y:.1f}) rotate({ang:.1f})"/>'
            s += f'<ellipse cx="0" cy="0" rx="10" ry="6.5" fill="none" stroke="url(#m-gold)" stroke-width="5" transform="translate({x:.1f} {y:.1f}) rotate({ang:.1f})"/>'
        else:
            s_d += f'<rect x="-10" y="-3.5" width="20" height="7" rx="3.5" fill="{O}" stroke="{O}" stroke-width="4" transform="translate({x:.1f} {y:.1f}) rotate({ang:.1f})"/>'
            s += f'<rect x="-9" y="-2.5" width="18" height="5" rx="2.5" fill="url(#m-gold)" transform="translate({x:.1f} {y:.1f}) rotate({ang:.1f})"/>'
    return s_d + s


def em6():  # KINGPIN: gold chain medallion
    studs = ""
    for i in range(20):
        a = math.radians(i * 18)
        x, y = 128 + 62 * math.cos(a), 146 + 62 * math.sin(a)
        studs += f'<circle cx="{x:.1f}" cy="{y:.1f}" r="4.6" fill="url(#m-ice)" stroke="{O}" stroke-width="1.6"/><circle cx="{x-1.4:.1f}" cy="{y-1.4:.1f}" r="1.4" fill="#fff"/>'
    return f'''
<circle cx="128" cy="140" r="118" fill="url(#aura-gold)"/>
{chain(30, 6, 120, 70, 9)}{chain(226, 6, 136, 70, 9)}
<rect x="114" y="60" width="28" height="26" rx="8" fill="url(#m-gold)" stroke="{O}" stroke-width="4.5"/>
<rect x="121" y="66" width="14" height="10" rx="4" fill="#3A2404"/>
<g filter="url(#f-drop)"><circle cx="128" cy="146" r="74" fill="url(#m-gold)" stroke="{O}" stroke-width="5"/></g>
<circle cx="128" cy="146" r="68" fill="url(#m-goldR)" stroke="{O}" stroke-width="2.5"/>
{studs}
<circle cx="128" cy="146" r="54" fill="url(#m-gold)" stroke="{O}" stroke-width="4"/>
<circle cx="128" cy="146" r="47" fill="url(#r-face-black)" stroke="{O}" stroke-width="3"/>
<text x="128" y="174" text-anchor="middle" font-family="Anton" font-size="78" fill="{O}" stroke="{O}" stroke-width="9" stroke-linejoin="round">K</text>
<text x="128" y="174" text-anchor="middle" font-family="Anton" font-size="78" fill="url(#m-gold)">K</text>
<path d="M150 112 l3 7 7 3 -7 3 -3 7 -3 -7 -7 -3 7 -3z" fill="#fff"/>
<path d="M70 118 A62 62 0 0 1 112 82" stroke="#FFF7D1" stroke-width="4" stroke-linecap="round" opacity="0.8" fill="none"/>
'''


def em7():  # DON OF THE BRONX: crown over the skyline in a platinum wreath
    leaves = ""
    for side in (-1, 1):
        leaves += f'<path d="M{128+side*30} 214 Q{128+side*96} 196 {128+side*92} 92" stroke="url(#m-plat)" stroke-width="4" fill="none"/>'
        for k in range(10):
            a = 30 + k * 13.5
            for off, tilt in ((-7, 34), (7, -6)):
                r = 86 + off * 0.9
                ar = math.radians(a)
                x = 128 - side * r * math.sin(ar)
                y = 142 + r * math.cos(ar)
                th = (a - 90 - tilt) * (-side)
                sc = 1.0 - k * 0.03
                leaves += (f'<path d="M0 0 C8 -8 8 -24 0 -34 C-8 -24 -8 -8 0 0Z" transform="translate({x:.1f} {y:.1f}) rotate({th:.1f}) scale({sc:.2f})" '
                           f'fill="url(#m-plat)" stroke="{O}" stroke-width="3"/>')
                leaves += f'<path d="M0 -4 L0 -26" transform="translate({x:.1f} {y:.1f}) rotate({th:.1f}) scale({sc:.2f})" stroke="#6FA9C4" stroke-width="1.4"/>'
    # skyline (inside the face, clipped)
    sky = ("M60 214 V170 h14 v-16 h12 v-22 h16 v18 h10 v-30 h8 v-10 h6 v10 h8 v26 h12 v-14 h14 v24 h10 v-34 h18 v40 h14 v62 Z")
    wins = ""
    for (x, y) in [(66, 178), (90, 146), (92, 162), (106, 156), (130, 128), (130, 146), (142, 168), (158, 154), (176, 132), (182, 150), (190, 178), (120, 176), (74, 190)]:
        wins += f'<rect x="{x}" y="{y}" width="4" height="5" fill="#FFD27A"/>'
    tower = '<path d="M160 130 h22 v18 h-22 z M158 130 l13 -12 13 12 z M163 148 l-3 16 M179 148 l3 16 M162 156 h18" fill="url(#m-plat)" stroke="#0A0716" stroke-width="2"/>'
    return f'''
<circle cx="128" cy="140" r="122" fill="url(#aura-ice)"/>
{leaves}
<g filter="url(#f-drop)"><circle cx="128" cy="144" r="70" fill="url(#m-plat)" stroke="{O}" stroke-width="5"/></g>
<circle cx="128" cy="144" r="62" fill="url(#m-platR)" stroke="{O}" stroke-width="2.5"/>
<clipPath id="e7clip"><circle cx="128" cy="144" r="55"/></clipPath>
<circle cx="128" cy="144" r="55" fill="url(#r-face-night)"/>
<g clip-path="url(#e7clip)">
 <circle cx="128" cy="140" r="40" fill="#8FEBFF" opacity="0.18"/>
 <path d="{sky}" fill="#0A0716"/>
 {tower}
 <path d="{sky}" fill="none" stroke="#8FEBFF" stroke-width="2" opacity="0.8"/>
 {wins}
</g>
<circle cx="128" cy="144" r="55" fill="none" stroke="{O}" stroke-width="3"/>
<g filter="url(#f-drop)">
<path d="M74 92 L66 34 L100 62 L128 18 L156 62 L190 34 L182 92 Z" fill="url(#m-gold)" stroke="{O}" stroke-width="5" stroke-linejoin="round"/>
<path d="M100 62 L128 18 L156 62 L128 92 Z" fill="#FFE58A" opacity="0.35"/>
<rect x="70" y="84" width="116" height="20" rx="5" fill="url(#m-gold)" stroke="{O}" stroke-width="4.5"/>
</g>
<circle cx="66" cy="34" r="7.5" fill="url(#m-ice)" stroke="{O}" stroke-width="2.5"/><circle cx="190" cy="34" r="7.5" fill="url(#m-ice)" stroke="{O}" stroke-width="2.5"/>
<circle cx="128" cy="16" r="9" fill="url(#m-ice)" stroke="{O}" stroke-width="2.5"/>
<path d="M128 80 L140 94 L128 108 L116 94 Z" fill="url(#m-ice)" stroke="{O}" stroke-width="2.5" transform="translate(0 -1)"/>
<circle cx="96" cy="94" r="4.5" fill="url(#m-ice)" stroke="{O}" stroke-width="1.6"/><circle cx="160" cy="94" r="4.5" fill="url(#m-ice)" stroke="{O}" stroke-width="1.6"/>
<path d="M125 12 l2 -6 2 6" stroke="#fff" stroke-width="1.5" fill="none"/>
<path d="M68 128 A62 62 0 0 1 98 92" stroke="#fff" stroke-width="3.5" stroke-linecap="round" opacity="0.6" fill="none"/>
'''


FLAME = ("M128 4 C140 32 168 44 166 82 C178 70 182 56 178 44 C202 70 208 106 192 136 C178 162 152 172 128 172 "
         "C104 172 78 162 64 136 C48 106 54 70 78 44 C74 56 78 70 90 82 C88 44 116 32 128 4Z")


def em8():  # DEF JAM LEGEND: neon diamond crest, gold wings, live fire
    wings = ""
    for side in (-1, 1):
        for k in range(7):
            rot = side * (28 + k * 12)
            L = 1.32 - k * 0.09
            bx = 128 + side * (30 + k * 2)
            by = 150 - k * 3
            wings += (f'<path d="M0 0 C11 -16 13 -56 0 -88 C-6 -60 -12 -20 0 0Z" transform="translate({bx} {by}) rotate({rot}) scale({L:.2f} {L:.2f})" '
                      f'fill="url(#m-gold)" stroke="{O}" stroke-width="3.5"/>')
    crest = "M128 32 L196 126 L128 214 L60 126 Z"
    gem = [(128, 70), (164, 124), (128, 180), (92, 124)]
    c = (128, 116)
    facets = ""
    cols = ["#FFFFFF", "#9FE7FF", "#D5F6FF", "#5FC2E6"]
    for k in range(4):
        a, b = gem[k], gem[(k + 1) % 4]
        facets += f'<polygon points="{poly([c, a, b])}" fill="{cols[k]}"/>'
    facets += f'<polygon points="{poly([(110,98),(146,98),(128,116)])}" fill="#fff" opacity="0.9"/>'
    return f'''
<circle cx="128" cy="124" r="126" fill="url(#aura-fire)"><animate attributeName="opacity" values="0.65;1;0.65" dur="1.6s" repeatCount="indefinite"/></circle>
<g transform="translate(128 172) scale(1.18 1.22)"><g>
 <animateTransform attributeName="transform" type="scale" values="1 1;1.03 1.07;0.98 0.99;1 1" dur="0.9s" repeatCount="indefinite"/>
 <path d="{FLAME}" transform="translate(-128 -172)" fill="#FF2E4D" stroke="{O}" stroke-width="4"/>
 <path d="{FLAME}" transform="translate(-96 -129) scale(0.75)" fill="#FFA81A"/>
 <path d="{FLAME}" transform="translate(-64 -86) scale(0.5)" fill="#FFE07A"/>
</g></g>
<g fill="#FFC53D"><circle cx="70" cy="40" r="3"><animate attributeName="opacity" values="1;0.2;1" dur="1.3s" repeatCount="indefinite"/></circle><circle cx="192" cy="30" r="2.5"><animate attributeName="opacity" values="0.3;1;0.3" dur="1.1s" repeatCount="indefinite"/></circle><circle cx="206" cy="70" r="2"/><circle cx="52" cy="78" r="2"/><circle cx="160" cy="6" r="2.4"><animate attributeName="opacity" values="1;0.3;1" dur="0.9s" repeatCount="indefinite"/></circle></g>
{wings}
<path d="{crest}" fill="#FF3B54" filter="url(#f-glow)" opacity="0.9"><animate attributeName="opacity" values="0.55;1;0.55" dur="1.6s" repeatCount="indefinite"/></path>
<path d="{crest}" fill="url(#r-obsidian)" stroke="{O}" stroke-width="6" stroke-linejoin="round"/>
<path d="M128 44 L186 126 L128 202 L70 126 Z" fill="none" stroke="url(#l-neon)" stroke-width="5" stroke-linejoin="round"/>
<path d="M128 44 L186 126 L128 202 L70 126 Z" fill="none" stroke="#FFF1F3" stroke-width="1.5" stroke-linejoin="round" opacity="0.8"/>
<polygon points="{poly(gem)}" fill="#fff" filter="url(#f-glow2)" opacity="0.7"/>
{facets}
<polygon points="{poly(gem)}" fill="none" stroke="{O}" stroke-width="3.5" stroke-linejoin="round"/>
<path d="M128 70 L128 180 M92 124 L164 124" stroke="#0A2A3A" stroke-width="1" opacity="0.4"/>
<path d="M150 82 l3 8 8 3 -8 3 -3 8 -3 -8 -8 -3 8 -3z" fill="#fff"><animate attributeName="opacity" values="0;1;0" dur="2.4s" repeatCount="indefinite"/></path>
'''


# ---------------------------------------------------------------- small emblems (64 box)
def sm():
    s = {}
    s[1] = f'''
<rect x="5" y="7" width="54" height="52" rx="6" fill="url(#m-conc)" stroke="{O}" stroke-width="3"/>
<rect x="5" y="7" width="54" height="52" rx="6" fill="url(#p-brick)" opacity="0.5" transform="scale(1)"/>
<circle cx="32" cy="33" r="19" fill="none" stroke="#3FD47F" stroke-width="5" opacity="0.9"/>
<rect x="29" y="30" width="6" height="28" fill="#C9D2DB" stroke="{O}" stroke-width="2"/>
<path d="M2 18 L32 22 L32 36 L2 32 Z" fill="#14894D" stroke="{O}" stroke-width="3" stroke-linejoin="round"/>
<path d="M32 22 L62 32 L60 46 L32 36 Z" fill="#0A5A31" stroke="{O}" stroke-width="3" stroke-linejoin="round"/>
<path d="M6 22 L29 25" stroke="#EAF6EE" stroke-width="2.5"/><path d="M36 26 L58 34" stroke="#CFE3D6" stroke-width="2.5"/>'''
    s[2] = f'''
<circle cx="32" cy="32" r="27" fill="url(#r-face-steel)" stroke="{O}" stroke-width="3"/>
<circle cx="32" cy="32" r="25" fill="none" stroke="{O}" stroke-width="9"/>
<circle cx="32" cy="32" r="25" fill="none" stroke="url(#m-steel)" stroke-width="5" stroke-dasharray="9 4"/>
<g transform="rotate(-12 32 32)"><rect x="20" y="20" width="24" height="24" rx="5" fill="url(#l-ivory)" stroke="{O}" stroke-width="3"/>
<circle cx="26" cy="26" r="2.6" fill="#141016"/><circle cx="32" cy="32" r="2.6" fill="#141016"/><circle cx="38" cy="38" r="2.6" fill="#141016"/></g>'''
    hp = poly([(32, 3), (57, 17.5), (57, 46.5), (32, 61), (7, 46.5), (7, 17.5)])
    s[3] = f'''
<polygon points="{hp}" fill="url(#m-gun)" stroke="{O}" stroke-width="3" stroke-linejoin="round"/>
<g transform="translate(32 32) scale(0.78) translate(-32 -32)"><polygon points="{hp}" fill="url(#r-face-gun)" stroke="{O}" stroke-width="2"/></g>
<rect x="17" y="26" width="30" height="22" rx="6" fill="url(#m-tape)" stroke="{O}" stroke-width="2.5"/>
<rect x="16" y="16" width="9" height="16" rx="4" fill="url(#m-tape)" stroke="{O}" stroke-width="2.5"/>
<rect x="25" y="14" width="9" height="17" rx="4" fill="url(#m-tape)" stroke="{O}" stroke-width="2.5"/>
<rect x="34" y="15" width="9" height="16" rx="4" fill="url(#m-tape)" stroke="{O}" stroke-width="2.5"/>
<rect x="43" y="18" width="7" height="14" rx="3.5" fill="url(#m-tape)" stroke="{O}" stroke-width="2.5"/>
<path d="M16 40 L48 33 L48 39 L16 46 Z" fill="#E0302F" stroke="{O}" stroke-width="2"/>'''
    s[4] = f'''
<path d="M0 25 Q32 21 64 25 L64 39 Q32 43 0 39 Z" fill="#160C08" stroke="{O}" stroke-width="2.5"/>
<circle cx="32" cy="32" r="24" fill="url(#m-bronze)" stroke="{O}" stroke-width="3"/>
<circle cx="32" cy="32" r="17" fill="url(#r-face-red)" stroke="{O}" stroke-width="2.5"/>
{faceted_star(32, 33, 13, 5.5, "#FFD2A3", "#A65A26").replace('stroke-width="4"', 'stroke-width="2"').replace('stroke-width="2.5"', 'stroke-width="1.5"')}'''
    sh = "M32 4 L56 11 L54 35 C52 47 44 54 32 60 C20 54 12 47 10 35 L8 11 Z"
    rings = "".join(f'<circle cx="{x}" cy="29" r="4.6" fill="#050A16" stroke="url(#m-silver)" stroke-width="3"/>' for x in (21, 28.5, 36, 43.5))
    s[5] = f'''
<path d="{sh}" fill="url(#m-silver)" stroke="{O}" stroke-width="3" stroke-linejoin="round"/>
<g transform="translate(32 32) scale(0.8) translate(-32 -32)"><path d="{sh}" fill="url(#r-face-navy)" stroke="{O}" stroke-width="2"/></g>
{rings}
<path d="M16 33 Q32 31 48 33 L47 39 Q32 45 17 39 Z" fill="url(#m-silver)" stroke="{O}" stroke-width="2"/>'''
    s[6] = f'''
<path d="M6 0 L29 20 M58 0 L35 20" stroke="{O}" stroke-width="7"/><path d="M6 0 L29 20 M58 0 L35 20" stroke="url(#m-gold)" stroke-width="3.5" stroke-dasharray="4 2"/>
<circle cx="32" cy="38" r="24" fill="url(#m-gold)" stroke="{O}" stroke-width="3"/>
<circle cx="32" cy="38" r="17" fill="url(#r-face-black)" stroke="{O}" stroke-width="2"/>
<text x="32" y="49" text-anchor="middle" font-family="Anton" font-size="29" fill="url(#m-gold)" stroke="{O}" stroke-width="1">K</text>'''
    s[7] = f'''
<circle cx="32" cy="38" r="23" fill="url(#m-plat)" stroke="{O}" stroke-width="3"/>
<circle cx="32" cy="38" r="17" fill="url(#r-face-night)" stroke="{O}" stroke-width="2"/>
<path d="M17 52 v-10 h5 v-6 h5 v8 h4 v-12 h5 v10 h4 v-6 h5 v16 Z" fill="#8FEBFF" opacity="0.85"/>
<path d="M14 26 L10 6 L22 15 L32 1 L42 15 L54 6 L50 26 Z" fill="url(#m-gold)" stroke="{O}" stroke-width="3" stroke-linejoin="round"/>
<circle cx="32" cy="3" r="3.5" fill="url(#m-ice)" stroke="{O}" stroke-width="1.5"/>'''
    s[8] = f'''
<circle cx="32" cy="32" r="31" fill="url(#aura-fire)"><animate attributeName="opacity" values="0.6;1;0.6" dur="1.6s" repeatCount="indefinite"/></circle>
<path d="{FLAME}" transform="translate(32 44) scale(0.29) translate(-128 -172)" fill="#FF2E4D" stroke="{O}" stroke-width="8"/>
<path d="{FLAME}" transform="translate(32 44) scale(0.18) translate(-128 -172)" fill="#FFA81A"/>
<path d="M32 12 L52 37 L32 62 L12 37 Z" fill="url(#r-obsidian)" stroke="{O}" stroke-width="3" stroke-linejoin="round"/>
<path d="M32 17 L48 37 L32 57 L16 37 Z" fill="none" stroke="url(#l-neon)" stroke-width="2.5"/>
<path d="M32 26 L41 37 L32 50 L23 37 Z" fill="url(#m-ice)" stroke="{O}" stroke-width="1.5"/>'''
    return s


def chevrons(tier, lit, x=128, y=0):
    """Division chevrons for the 256 box (y 224..252). lit = 1..3 (III=1, II=2, I=3)."""
    m = TIER_METAL[tier]
    g = TIERS[tier - 1]["col"]
    out = ""
    for k, cx in enumerate((x - 40, x, x + 40)):
        d = f"M{cx-18} 242 L{cx} 224 L{cx+18} 242 L{cx+18} 254 L{cx} 236 L{cx-18} 254 Z"
        if k < lit:
            out += f'<path d="{d}" fill="{g}" filter="url(#f-glow)" opacity="0.75"/>'
            out += f'<path d="{d}" fill="{g}" stroke="{O}" stroke-width="3" stroke-linejoin="round"/>'
            out += f'<path d="M{cx-15} 241 L{cx} 226 L{cx+15} 241" fill="none" stroke="#fff" stroke-width="2" opacity="0.75"/>'
        else:
            out += f'<path d="{d}" fill="#18161C" stroke="rgba(255,255,255,0.28)" stroke-width="2" stroke-linejoin="round"/>'
    return f'<g transform="translate(0 {y})">{out}</g>'


def legend_plate(text, y=0):
    return f'''<g transform="translate(0 {y})"><path d="M70 228 H186 L176 254 H80 Z" fill="#12060A" stroke="url(#l-neon)" stroke-width="2.5" stroke-linejoin="round"/>
<text x="128" y="249" text-anchor="middle" font-family="Anton" font-size="20" letter-spacing="1.5" fill="#FFE9B0">{text}</text></g>'''


def defs_svg():
    syms = [sym(i, f()) for i, f in enumerate([em1, em2, em3, em4, em5, em6, em7, em8], start=1)]
    smalls = [sym(i, b, 64, "sm") for i, b in sm().items()]
    return (f'<svg aria-hidden="true" width="0" height="0" style="position:absolute;width:0;height:0;overflow:hidden"><defs>{grads()}'
            + "".join(syms) + "".join(smalls) + "</defs></svg>")


def big(tier, size, lit=None, plate=None, extra_style=""):
    over = ""
    if tier == 8:
        over = legend_plate(plate or "2214") if plate is not False else ""
    elif lit:
        over = chevrons(tier, lit)
    return (f'<svg width="{size}" height="{size}" viewBox="0 0 256 256" role="img" aria-label="{TIERS[tier-1]["name"].title()} emblem" '
            f'style="overflow:visible;{extra_style}"><use href="#em-{tier}"/>{over}</svg>')


def small(tier, size, extra_style=""):
    return (f'<svg width="{size}" height="{size}" viewBox="0 0 64 64" role="img" aria-label="{TIERS[tier-1]["name"].title()}" '
            f'style="overflow:visible;flex:none;{extra_style}"><use href="#sm-{tier}"/></svg>')
