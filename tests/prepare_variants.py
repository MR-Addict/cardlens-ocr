"""Generate transformed regression inputs. Independent evaluation images use their own manifest."""
from pathlib import Path
import json
from PIL import Image, ImageDraw, ImageEnhance, ImageFilter

ROOT = Path(__file__).resolve().parents[1]
folder = ROOT / 'out/testdata'
rows = []
for case in json.loads((ROOT / 'tests/gold.json').read_text(encoding='utf8'))['cases']:
    image = Image.open(folder / case['file']).convert('RGB')
    variants = [(f'rotate-{angle}', image.rotate(angle, expand=True), True) for angle in (90, 180, 270)]
    variants += [('small', image.resize((max(64, image.width * 2 // 3), max(64, image.height * 2 // 3))), False),
                 ('blur', image.filter(ImageFilter.GaussianBlur(1.5)), False),
                 ('dark', ImageEnhance.Brightness(image).enhance(.55), False)]
    background = Image.new('RGB', (image.width + 160, image.height + 120), '#69706b')
    background.paste(image, (80, 60)); variants.append(('background', background, False))
    # Projective shear creates a deterministic slanted card with a preserved full canvas.
    perspective = image.transform((image.width + 100, image.height + 80), Image.Transform.PERSPECTIVE,
        (1, -.12, 0, -.04, 1, 0, -.00003, .00004), Image.Resampling.BICUBIC, fillcolor='#69706b')
    variants.append(('perspective', perspective, False))
    for condition in ('glare', 'occlusion'):
        altered = image.copy(); draw = ImageDraw.Draw(altered)
        x, y = image.width * .25, image.height * .4
        draw.rectangle((x, y, x + image.width * .45, y + image.height * .18), fill='white' if condition == 'glare' else '#69706b')
        variants.append((condition, altered, False))
    for condition, variant, strict in variants:
        name = f"{Path(case['file']).stem}-{condition}.png"
        variant.save(folder / name)
        rows.append({**case, 'file': name, 'split': 'transformed-regression', 'condition': condition, 'strict': strict})
    wrong_type = 'driver_license' if case['type'] != 'driver_license' else 'vehicle_license'
    rows.append({'file': case['file'], 'type': wrong_type, 'side': 'main', 'status': 'failed',
                 'split': 'negative-regression', 'condition': 'wrong-type'})
    multiple = Image.new('RGB', (image.width * 2 + 180, image.height + 120), '#69706b')
    multiple.paste(image, (60, 60)); multiple.paste(image, (image.width + 120, 60))
    name = f"{Path(case['file']).stem}-multiple.png"; multiple.save(folder / name)
    rows.append({'file': name, 'type': case['type'], 'side': case['side'], 'status': 'failed',
                 'split': 'negative-regression', 'condition': 'multiple-documents'})
    if case['type'] == 'id_card':
        rows.append({'file': case['file'], 'type': 'id_card',
                     'side': 'emblem' if case['side'] == 'portrait' else 'portrait', 'status': 'failed',
                     'split': 'negative-regression', 'condition': 'wrong-side'})
Image.new('RGB', (800, 600), 'white').save(folder / 'blank.png')
rows.append({'file': 'blank.png', 'type': 'id_card', 'side': 'portrait', 'status': 'failed',
             'split': 'negative-regression', 'condition': 'blank'})
(folder / 'variants.json').write_text(json.dumps({'cases': rows}, ensure_ascii=False, indent=2) + '\n', encoding='utf8')
print(f'Prepared {len(rows)} cases')
