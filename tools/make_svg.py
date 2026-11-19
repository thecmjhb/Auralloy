#!/usr/bin/env python3
"""Generates the vector brand assets: logo.svg, wordmark.svg, the animated banner.svg, the animated pipeline.svg
and the 20 language chips. Pure standard library.   python tools/make_svg.py"""
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "assets")
os.makedirs(os.path.join(OUT, "languages"), exist_ok=True)

MINT, VIOLET, ROSE = "#5cf2d6", "#9b8cff", "#ff6fb5"

# --- geometry shared with src/app/Theme.cpp (the app draws exactly this) ---------------------------------
def mark_defs(uid):
    return f"""<linearGradient id="gl{uid}" gradientUnits="userSpaceOnUse" x1="58" y1="344" x2="195" y2="64"><stop offset="0" stop-color="{MINT}"/><stop offset="1" stop-color="{VIOLET}"/></linearGradient>
<linearGradient id="gr{uid}" gradientUnits="userSpaceOnUse" x1="342" y1="344" x2="205" y2="64"><stop offset="0" stop-color="{ROSE}"/><stop offset="1" stop-color="{VIOLET}"/></linearGradient>
<radialGradient id="ap{uid}" cx="50%" cy="50%" r="50%"><stop offset="0" stop-color="#fff" stop-opacity=".95"/><stop offset="1" stop-color="{VIOLET}" stop-opacity="0"/></radialGradient>
<radialGradient id="gw{uid}" cx="50%" cy="50%" r="50%"><stop offset="0" stop-color="{VIOLET}" stop-opacity=".55"/><stop offset="1" stop-color="{VIOLET}" stop-opacity="0"/></radialGradient>
<linearGradient id="br{uid}" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#fff"/><stop offset="1" stop-color="#d9d4ff"/></linearGradient>"""

BARS = [(0.070, 0), (0.130, 1), (0.195, 2), (0.110, 3), (0.065, 4)]

def mark_body(uid, anim=False):
    legs = ""
    for side, g, x0, x1 in (("l", "gl", 58, 195.2), ("r", "gr", 342, 204.8)):
        cls = f' class="leg {side}"' if anim else ""
        legs += (f'<path{cls} d="M{x0} 344 L{x1} 64" pathLength="1" fill="none" stroke="url(#{g}{uid})" stroke-width="66" stroke-linecap="round"/>'
                 f'<path d="M{x0} 344 L{x1} 64" fill="none" stroke="#fff" stroke-opacity=".28" stroke-width="2" stroke-linecap="round" transform="translate(0 0)"{" class=%sleg l2%s" % (chr(34), chr(34)) if False else ""}/>')
    bars = ""
    for h, i in BARS:
        hh = h * 2 * 400
        cls = f' class="bar b{i}"' if anim else ""
        bars += f'<rect{cls} x="{200 + (i - 2) * 25.6 - 6.8:.1f}" y="{246 - hh / 2:.1f}" width="13.6" height="{hh:.1f}" rx="6.8" fill="url(#br{uid})"/>'
    apex_cls = ' class="apex"' if anim else ""
    return f'<circle cx="200" cy="200" r="290" fill="url(#gw{uid})"{" class=%sglow%s" % (chr(34), chr(34)) if anim else ""}/>{legs}<ellipse{apex_cls} cx="200" cy="82" rx="34" ry="30" fill="url(#ap{uid})"/>{bars}'

def logo_svg():
    return f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 400 400" width="400" height="400" role="img" aria-label="Auralloy"><title>Auralloy</title><defs>{mark_defs("")}</defs>{mark_body("")}</svg>\n'

# --- wordmark ---------------------------------------------------------------------------------------------
LETTERS = {
    "A": '<path d="M2 56 L20 2 L38 56 M9.5 38 L30.5 38"/>',
    "U": '<path d="M2 2 V38 C2 47.94 10.06 56 20 56 C29.94 56 38 47.94 38 38 V2"/>',
    "R": '<path d="M2 56 V2 H20 C27.73 2 34 8.27 34 16 C34 23.73 27.73 30 20 30 H2 M19 30 L37 56"/>',
    "L": '<path d="M2 2 V56 H36"/>',
    "O": '<ellipse cx="20" cy="29" rx="18" ry="27"/>',
    "Y": '<path d="M2 2 L20 30 L38 2 M20 30 V56"/>',
}
WORD = "AURALLOY"
TOTAL = 8 * 40 + 7 * 17

def wordmark_defs(uid):
    out = ""
    for i in range(len(WORD)):
        out += (f'<linearGradient id="wg{uid}{i}" gradientUnits="userSpaceOnUse" x1="{-i * 57}" y1="0" x2="{TOTAL - i * 57}" y2="0">'
                f'<stop offset="0" stop-color="{MINT}"/><stop offset=".5" stop-color="{VIOLET}"/><stop offset="1" stop-color="{ROSE}"/></linearGradient>')
    return out

def wordmark_group(anim=False, uid="w"):
    out = ""
    for i, ch in enumerate(WORD):
        el = LETTERS[ch].replace("<path ", '<path pathLength="1" ').replace("<ellipse ", '<ellipse pathLength="1" ')
        if anim:
            el = el.replace("/>", f' class="wl w{i}"/>', 1)
        out += f'<g transform="translate({i * 57} 0)" stroke="url(#wg{uid}{i})">{el}</g>'
    return f'<g fill="none" stroke-width="7" stroke-linecap="round" stroke-linejoin="round">{out}</g>'

def wordmark_svg():
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="-6 -6 {TOTAL + 12} 68" width="{TOTAL + 12}" height="68" role="img" aria-label="Auralloy"><title>Auralloy</title>'
            f'<defs>{wordmark_defs("")}</defs>'
            f'{wordmark_group(False, "")}</svg>\n')

# --- animated banner -----------------------------------------------------------------------------------------
def keyframes(name, steps):
    """steps: list of (percent, css-declarations)"""
    return f"@keyframes {name}{{" + "".join(f"{p}%{{{d}}}" for p, d in steps) + "}"

def banner_svg():
    D = 11  # seconds per loop
    css = []
    # legs draw in from the bottom (pathLength=1)
    for side in ("l", "r"):
        css.append(keyframes(f"leg{side}", [(0, "stroke-dashoffset:1;opacity:0"), (2, "opacity:1;stroke-dashoffset:1"), (14, "stroke-dashoffset:0"), (93, "stroke-dashoffset:0;opacity:1"), (97, "opacity:0;stroke-dashoffset:0"), (100, "opacity:0;stroke-dashoffset:1")]))
        css.append(f".leg.{side}{{stroke-dasharray:1;animation:leg{side} {D}s cubic-bezier(.16,1,.3,1) infinite}}")
    css.append(keyframes("apex", [(0, "opacity:0"), (13, "opacity:0"), (16, "opacity:1"), (22, "opacity:.45"), (93, "opacity:.45"), (97, "opacity:0"), (100, "opacity:0")]))
    css.append(f".apex{{animation:apex {D}s ease-out infinite}}")
    css.append(keyframes("glow", [(0, "opacity:0"), (14, "opacity:.3"), (17, "opacity:1"), (30, "opacity:.55"), (93, "opacity:.55"), (97, "opacity:0"), (100, "opacity:0")]))
    css.append(f".glow{{animation:glow {D}s ease-out infinite}}")
    for i in range(5):
        a = 15 + i * 1.2
        css.append(keyframes(f"bar{i}", [(0, "transform:scaleY(0)"), (a, "transform:scaleY(0)"), (a + 5, "transform:scaleY(1)"), (a + 12, "transform:scaleY(.55)"), (a + 20, "transform:scaleY(1.15)"), (a + 30, "transform:scaleY(.8)"), (a + 40, "transform:scaleY(1)"), (93, "transform:scaleY(1)"), (97, "transform:scaleY(0)"), (100, "transform:scaleY(0)")]))
        css.append(f".bar.b{i}{{transform-box:fill-box;transform-origin:50% 50%;animation:bar{i} {D}s ease-in-out infinite}}")
    # wordmark letters draw
    for i in range(8):
        s = 20 + i * 2.2
        css.append(keyframes(f"wl{i}", [(0, "stroke-dashoffset:1;opacity:0"), (s, "stroke-dashoffset:1;opacity:0"), (s + .5, "opacity:1;stroke-dashoffset:1"), (s + 5, "stroke-dashoffset:0"), (93, "stroke-dashoffset:0;opacity:1"), (97, "opacity:0"), (100, "opacity:0;stroke-dashoffset:1")]))
        css.append(f".wl.w{i}{{stroke-dasharray:1;animation:wl{i} {D}s cubic-bezier(.16,1,.3,1) infinite}}")
    for name, a in (("tag", 40), ("c0", 46), ("c1", 49), ("c2", 52), ("c3", 55)):
        css.append(keyframes(name, [(0, "opacity:0;transform:translateY(8px)"), (a, "opacity:0;transform:translateY(8px)"), (a + 5, "opacity:1;transform:translateY(0)"), (93, "opacity:1;transform:translateY(0)"), (97, "opacity:0"), (100, "opacity:0")]))
        css.append(f".{name}{{animation:{name} {D}s cubic-bezier(.16,1,.3,1) infinite}}")
    # aurora drift
    css.append("@keyframes d1{0%{transform:translate(0,0)}50%{transform:translate(160px,50px)}100%{transform:translate(0,0)}}")
    css.append("@keyframes d2{0%{transform:translate(0,0)}50%{transform:translate(-140px,40px)}100%{transform:translate(0,0)}}")
    css.append("@keyframes d3{0%{transform:translate(0,0)}50%{transform:translate(70px,-40px)}100%{transform:translate(0,0)}}")
    css.append(".o1{animation:d1 14s ease-in-out infinite}.o2{animation:d2 17s ease-in-out infinite}.o3{animation:d3 12s ease-in-out infinite}")
    # moving sheen on the border
    css.append("@keyframes sh{0%{stroke-dashoffset:0}100%{stroke-dashoffset:-2400}}.sheen{stroke-dasharray:240 2160;animation:sh 9s linear infinite}")

    chips = ""
    for i, (t, c, w) in enumerate((("Auto-mashup", MINT, 150), ("Slowed + Reverb", ROSE, 190), ("Timeline editor", VIOLET, 180), ("20 languages", "#ffc857", 160))):
        x = 520 + sum(((0, 150, 190, 180)[j] + 12) for j in range(i + 1) if j > 0) if i > 0 else 520
        chips += (f'<g class="c{i}"><rect x="{x}" y="318" width="{w}" height="38" rx="19" fill="{c}" fill-opacity=".16" stroke="{c}" stroke-opacity=".9" stroke-width="1.4"/>'
                  f'<text x="{x + w / 2}" y="342" text-anchor="middle" fill="#edeff8" font-size="15" font-weight="600" font-family="Segoe UI, Inter, Helvetica, Arial, sans-serif">{t}</text></g>')

    return f"""<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1280 420" width="1280" height="420" role="img" aria-label="Auralloy - two songs in, one alloy out">
<title>Auralloy - two songs in, one alloy out</title>
<defs>{mark_defs("b")}
<linearGradient id="bg" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#05060a"/><stop offset="1" stop-color="#0d0b1a"/></linearGradient>
<radialGradient id="o1" cx="50%" cy="50%" r="50%"><stop offset="0" stop-color="{VIOLET}" stop-opacity=".42"/><stop offset="1" stop-color="{VIOLET}" stop-opacity="0"/></radialGradient>
<radialGradient id="o2" cx="50%" cy="50%" r="50%"><stop offset="0" stop-color="{ROSE}" stop-opacity=".3"/><stop offset="1" stop-color="{ROSE}" stop-opacity="0"/></radialGradient>
<radialGradient id="o3" cx="50%" cy="50%" r="50%"><stop offset="0" stop-color="{MINT}" stop-opacity=".22"/><stop offset="1" stop-color="{MINT}" stop-opacity="0"/></radialGradient>
{wordmark_defs("b")}
<linearGradient id="rim" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#fff" stop-opacity=".45"/><stop offset=".5" stop-color="#fff" stop-opacity=".06"/><stop offset="1" stop-color="#fff" stop-opacity=".25"/></linearGradient>
<clipPath id="clip"><rect width="1280" height="420" rx="28"/></clipPath>
<style>{"".join(css)}</style></defs>
<g clip-path="url(#clip)">
<rect width="1280" height="420" fill="url(#bg)"/>
<circle class="o1" cx="260" cy="120" r="420" fill="url(#o1)"/>
<circle class="o2" cx="1060" cy="70" r="360" fill="url(#o2)"/>
<circle class="o3" cx="720" cy="430" r="320" fill="url(#o3)"/>
<g transform="translate(70 10) scale(.98)">{mark_body("b", anim=True)}</g>
<g transform="translate(510 120) scale(1.18)">{wordmark_group(True, "b")}</g>
<text class="tag" x="516" y="256" fill="#edeff8" font-size="32" font-weight="600" font-family="Segoe UI, Inter, Helvetica, Arial, sans-serif">Two songs in, one alloy out.</text>
<text class="tag" x="517" y="290" fill="#a9aec4" font-size="16" font-family="Segoe UI, Inter, Helvetica, Arial, sans-serif">Rule-based, explainable auto-mashups with an editable timeline. Slowed + reverb. Lofi.</text>
{chips}
</g>
<rect x="1" y="1" width="1278" height="418" rx="27" fill="none" stroke="url(#rim)" stroke-width="1.5"/>
<rect class="sheen" x="1" y="1" width="1278" height="418" rx="27" fill="none" stroke="#9b8cff" stroke-opacity=".55" stroke-width="2" pathLength="2400"/>
</svg>
"""

# --- animated pipeline -----------------------------------------------------------------------------------------
def pipeline_svg():
    nodes = [("Songs", "2 or more", MINT), ("Analyse", "tempo / key / bars", VIOLET), ("Stems", "vocals drums bass other", ROSE),
             ("Rules", "plan + explain", "#ffc857"), ("Timeline", "editable clips", MINT), ("Export", "WAV FLAC MP3", VIOLET)]
    W, H, NW, GAP = 1280, 200, 170, 52
    x0 = (W - (len(nodes) * NW + (len(nodes) - 1) * GAP)) / 2
    css = ["@keyframes flow{to{stroke-dashoffset:-24}}.arrow{stroke-dasharray:6 6;animation:flow 1.2s linear infinite}"]
    parts = ""
    D = 7.2
    for i, (t, s, c) in enumerate(nodes):
        x = x0 + i * (NW + GAP)
        a = i * 12
        css.append(keyframes(f"n{i}", [(0, "opacity:.55;transform:translateY(0)"), (a, "opacity:.55;transform:translateY(0)"), (a + 4, "opacity:1;transform:translateY(-5px)"), (a + 12, "opacity:1;transform:translateY(0)"), (100, "opacity:.9;transform:translateY(0)")]))
        css.append(f".n{i}{{animation:n{i} {D}s ease-in-out infinite}}")
        parts += (f'<g class="n{i}"><rect x="{x:.0f}" y="48" width="{NW}" height="104" rx="22" fill="{c}" fill-opacity=".10" stroke="{c}" stroke-opacity=".85" stroke-width="1.5"/>'
                  f'<text x="{x + NW / 2:.0f}" y="94" text-anchor="middle" fill="#edeff8" font-size="22" font-weight="700" font-family="Segoe UI, Inter, Helvetica, Arial, sans-serif">{t}</text>'
                  f'<text x="{x + NW / 2:.0f}" y="122" text-anchor="middle" fill="#a9aec4" font-size="13" font-family="Segoe UI, Inter, Helvetica, Arial, sans-serif">{s}</text></g>')
        if i < len(nodes) - 1:
            ax = x + NW + 6
            parts += f'<path class="arrow" d="M{ax:.0f} 100 H{ax + GAP - 12:.0f}" stroke="{c}" stroke-width="2.5" stroke-linecap="round" fill="none"/><path d="M{ax + GAP - 18:.0f} 93 L{ax + GAP - 10:.0f} 100 L{ax + GAP - 18:.0f} 107" stroke="{c}" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round" fill="none"/>'
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}" role="img" aria-label="Auralloy pipeline: songs, analysis, stems, rules, timeline, export">'
            f'<title>Auralloy pipeline</title><defs><style>{"".join(css)}</style></defs><rect width="{W}" height="{H}" rx="26" fill="#0a0c14"/>{parts}</svg>\n')

# --- language chips (same look as the author's other repositories) ----------------------------------------------
LANGS = [("en", "English"), ("bn", "বাংলা"), ("zh-CN", "中文"), ("hi", "हिन्दी"), ("es", "Español"), ("ar", "العربية"), ("fr", "Français"),
         ("pt", "Português"), ("id", "Bahasa Indonesia"), ("ur", "اردو"), ("ru", "Русский"), ("ko", "한국어"), ("ja", "日本語"), ("de", "Deutsch"),
         ("tr", "Türkçe"), ("it", "Italiano"), ("fa", "فارسی"), ("vi", "Tiếng Việt"), ("ta", "தமிழ்"), ("te", "తెలుగు")]

def lang_svg(code, label):
    w = 58 if len(label) < 9 else 64
    fs = 11 if len(label) < 12 else 8.5
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="28" viewBox="0 0 {w} 28" role="img" aria-label="{label}">\n'
            f'  <title>{label}</title>\n  <rect x="2.5" y="2.5" width="{w - 5}" height="23" rx="6" fill="#161b22" stroke="#3d5875"/>\n'
            f'  <text x="{w / 2}" y="18" text-anchor="middle" font-family="Segoe UI, Arial, sans-serif" font-size="{fs}" font-weight="500" fill="#e6edf3">{label}</text>\n</svg>\n')

def write(path, s):
    with open(os.path.join(OUT, path), "w", encoding="utf-8", newline="\n") as f:
        f.write(s)

write("logo.svg", logo_svg())
write("wordmark.svg", wordmark_svg())
write("banner.svg", banner_svg())
write("pipeline.svg", pipeline_svg())
for code, label in LANGS:
    write(os.path.join("languages", code + ".svg"), lang_svg(code, label))
print("assets written to", OUT)
