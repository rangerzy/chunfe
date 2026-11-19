"""Create the browser icon index from the same official SVGs used by Qt."""
import json
from pathlib import Path
import shutil

repo = Path(__file__).resolve().parents[4]
source = repo / 'modules/ui_controls/icons'
assets = Path(__file__).resolve().parent.parent
icons = {}
for svg in sorted(source.glob('*.svg')):
    inner = svg.read_text().split('<svg', 1)[1].split('>', 1)[1].rsplit('</svg>', 1)[0]
    icons[svg.stem] = inner.replace('#18181b', 'currentColor')
aliases = {
    'grid': 'home', 'sliders': 'settings', 'chip': 'audio', 'help': 'user',
    'arrow': 'arrow-right', 'chevron': 'chevron-right', 'back': 'chevron-left',
    'checkCircle': 'check', 'monitor': 'headphones', 'link': 'model',
    'shield': 'check', 'message': 'history', 'sparkle': 'model',
    'refresh': 'play', 'leaf': 'file', 'folder': 'file',
}
content = '''/* Official IconPark @icon-park/svg 1.4.2, Apache-2.0. Geometry unchanged; color uses currentColor. */
(() => {
  const paths = ''' + json.dumps(icons, ensure_ascii=False, indent=2) + ''';
  const aliases = ''' + json.dumps(aliases) + ''';
  window.ChunfeIcons = {icon(name, className = "") {
    const safeClass = String(className).replace(/[^a-zA-Z0-9_ -]/g, "");
    return `<svg class="${safeClass}" width="20" height="20" viewBox="0 0 48 48" fill="none" aria-hidden="true">${paths[aliases[name] || name] || paths.file}</svg>`;
  }};
})();
'''
(assets / 'iconpark.js').write_text(content)
for name in ('LICENSE', 'NOTICE', 'SOURCE.json'):
    shutil.copyfile(source / name, assets / 'iconpark' / name)
