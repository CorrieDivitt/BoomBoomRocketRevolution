"""Extract the original HLSL source embedded in Xbox 360 shader PDBs (*.updb).
Usage: python tools/shaders/extract_updb_hlsl.py "Game Files/shaders" extracted/shaders_hlsl
Output is game-derived material: it stays local (extracted/ is git-ignored)."""
import sys, os, re, html
src, dst = sys.argv[1], sys.argv[2]
os.makedirs(dst, exist_ok=True)
for fn in sorted(os.listdir(src)):
    if not fn.lower().endswith('.updb'): continue
    text = open(os.path.join(src, fn), encoding='utf-8', errors='replace').read()
    for m in re.finditer(r'<file id="\d+" path="([^"]+)"[^>]*>(.*?)</file>', text, re.S):
        name = m.group(1).replace('\\', '/').split('/')[-1]
        out = os.path.join(dst, name)
        if not os.path.exists(out):
            open(out, 'w', encoding='utf-8').write(html.unescape(m.group(2)))
            print(f'{fn} -> {name}')
