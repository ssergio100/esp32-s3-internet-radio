#!/usr/bin/env python3
from pathlib import Path
import json
import re
ROOT = Path(__file__).resolve().parents[1]
x = json.loads((ROOT/'config/ui_tokens.json').read_text())
assert x['resolution']=={'width':320,'height':240}
assert sum(x['layout'].values())==240
assert len(x['themes'])==5
for name, theme in x['themes'].items():
    assert set(theme)=={'primary','secondary','muted','surface','background'}
    assert all(re.fullmatch(r'#[0-9A-Fa-f]{6}', value) for value in theme.values())
css=(ROOT/'web/ui_themes.css').read_text()
header=(ROOT/'src/ui_theme_generated.hpp').read_text()
for name in x['themes']:
    assert '.theme-'+name in css
    assert name in header
print('Tokens, CSS e header: OK')
