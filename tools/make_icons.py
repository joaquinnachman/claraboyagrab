# Arma los recursos gráficos de Claraboya Grab a partir de la claraboya bordó.
# Uso: python3 tools/make_icons.py Resources/brand/forma-claraboya-bordo.png <carpeta de salida>
# Después: copiar icon-1024.png -> icono-1024.png, icon-small-256.png -> icono-chico-256.png
# y logo-ui.png reducido a 96 px de alto -> logo-cabecera.png (en Resources/brand/).
import sys
from PIL import Image, ImageDraw, ImageFilter

src, out = sys.argv[1], sys.argv[2]
BG = (242, 242, 242)
BORDO = (85, 17, 18)
DARK = (31, 36, 38)
WHITE = (242, 242, 242)

# 1) Recortar la claraboya con transparencia (des-mezclando el fondo gris claro).
im = Image.open(src).convert('RGB')
w, h = im.size
px = im.load()
mark = Image.new('RGBA', (w, h))
mp = mark.load()
for y in range(h):
    for x in range(w):
        r, g, b = px[x, y]
        # verde: 242 en el fondo, ~9 en la claraboya
        a = max(0.0, min(1.0, (BG[1] - g) / (BG[1] - 6.0)))
        if a <= 0.0:
            mp[x, y] = (0, 0, 0, 0)
            continue
        fr = (r - (1 - a) * BG[0]) / a
        fg = (g - (1 - a) * BG[1]) / a
        fb = (b - (1 - a) * BG[2]) / a
        clamp = lambda v: int(max(0, min(255, round(v))))
        mp[x, y] = (clamp(fr), clamp(fg), clamp(fb), int(round(a * 255)))
mark = mark.crop(mark.getbbox())
mark.save(f'{out}/claraboya-mark-full.png')
print('mark', mark.size)

# 2) Diskette genérico (dibujado a mano, sin copiar ningún set de íconos).
def floppy(size, body, shutter, label, slot, ss=4):
    S = size * ss
    img = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    r = S * 0.10
    cut = S * 0.20
    # cuerpo con la esquina superior derecha cortada
    pts = [(r, 0), (S - cut, 0), (S, cut), (S, S - r), (S - r, S), (r, S), (0, S - r), (0, r)]
    d.polygon(pts, fill=body)
    for cx, cy in [(r, r), (r, S - r), (S - r, S - r)]:
        d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=body)
    # chapa metálica arriba
    sx0, sx1, sy1 = S * 0.24, S * 0.70, S * 0.36
    d.rounded_rectangle([sx0, 0, sx1, sy1], radius=S * 0.03, fill=shutter)
    d.rounded_rectangle([S * 0.54, S * 0.06, S * 0.63, S * 0.29], radius=S * 0.015, fill=slot)
    # etiqueta abajo
    d.rounded_rectangle([S * 0.16, S * 0.52, S * 0.84, S * 0.94], radius=S * 0.05, fill=label)
    for i, yy in enumerate([0.64, 0.74, 0.84]):
        d.rounded_rectangle([S * 0.26, S * (yy - 0.018), S * (0.74 if i < 2 else 0.58), S * (yy + 0.018)],
                            radius=S * 0.018, fill=slot if label != slot else body)
    return img.resize((size, size), Image.LANCZOS)

def shadow(img, radius, alpha=110):
    pad = int(radius * 3) + 1
    a = img.split()[3].point(lambda v: v * alpha // 255)
    big = Image.new('L', (img.size[0] + 2 * pad, img.size[1] + 2 * pad), 0)
    big.paste(a, (pad, pad))
    sh = Image.new('RGBA', big.size, (0, 0, 0, 0))
    sh.putalpha(big.filter(ImageFilter.GaussianBlur(radius)))
    return sh, pad

def compose(size, tile, mark_scale, floppy_scale, fl_colors, ring_color=None, inset=0.05, mark_x=0.06):
    S = size
    canvas = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    if tile is not None:
        t = Image.new('RGBA', (S * 4, S * 4), (0, 0, 0, 0))
        ImageDraw.Draw(t).rounded_rectangle([0, 0, S * 4 - 1, S * 4 - 1], radius=S * 4 * 0.22, fill=tile)
        canvas.alpha_composite(t.resize((S, S), Image.LANCZOS))
    mh = int(S * mark_scale)
    mw = int(mark.size[0] * mh / mark.size[1])
    m = mark.resize((mw, mh), Image.LANCZOS)
    mx = int(S * mark_x)
    my = int(S * 0.5 - mh * 0.5)
    canvas.alpha_composite(m, (mx, my))
    fs = int(S * floppy_scale)
    f = floppy(fs, *fl_colors)
    fx, fy = int(S * (1 - inset) - fs), int(S * (1 - inset) - fs)
    if ring_color is not None:
        ring = int(S * 0.035)
        rr = Image.new('RGBA', (fs + 2 * ring, fs + 2 * ring), (0, 0, 0, 0))
        ImageDraw.Draw(rr).rounded_rectangle([0, 0, fs + 2 * ring - 1, fs + 2 * ring - 1], radius=ring * 2.2, fill=ring_color)
        canvas.alpha_composite(rr, (fx - ring, fy - ring))
    sh, pad = shadow(f, S * 0.012)
    canvas.alpha_composite(sh, (fx - pad, fy - pad + int(S * 0.008)))
    canvas.alpha_composite(f, (fx, fy))
    return canvas

# Ícono de la app: claraboya bordó sobre baldosa clara + diskette oscuro en la esquina.
fl_dark = (DARK + (255,), (140, 147, 150, 255), WHITE + (255,), DARK + (255,))
icon = compose(1024, BG + (255,), 0.70, 0.38, fl_dark, ring_color=BG + (255,), inset=0.10, mark_x=0.13)
icon.save(f'{out}/icon-1024.png')
compose(256, BG + (255,), 0.72, 0.44, fl_dark, ring_color=BG + (255,), inset=0.08, mark_x=0.10).save(f'{out}/icon-small-256.png')

# Logo para la cabecera (fondo oscuro): claraboya + diskette claro con aro del color del fondo.
fl_light = (WHITE + (255,), (150, 157, 160, 255), BORDO + (255,), WHITE + (255,))
ui = compose(260, None, 0.90, 0.40, fl_light, ring_color=DARK + (255,), inset=0.0, mark_x=0.02)
ui = ui.crop(ui.getbbox())
ui.save(f'{out}/logo-ui.png')
print('ui', ui.size)

# Hoja de prueba
sheet = Image.new('RGBA', (1500, 560), DARK + (255,))
sheet.alpha_composite(icon.resize((512, 512), Image.LANCZOS), (20, 24))
for i, s in enumerate([256, 64, 48, 32, 16]):
    src_ic = icon if s > 64 else Image.open(f'{out}/icon-small-256.png')
    sheet.alpha_composite(src_ic.resize((s, s), Image.LANCZOS), (560 + sum([256, 64, 48, 32, 16][:i]) + i * 20, 40))
light = Image.new('RGBA', (500, 200), (60, 60, 60, 255))
sheet.alpha_composite(light, (560, 330))
for i, s in enumerate([64, 32, 16]):
    sheet.alpha_composite(Image.open(f'{out}/icon-small-256.png').resize((s, s), Image.LANCZOS), (580 + i * 90, 360))
sheet.alpha_composite(ui.resize((int(ui.size[0] * 52 / ui.size[1]), 52), Image.LANCZOS), (1100, 360))
sheet.alpha_composite(ui, (1100, 40))
sheet.save(f'{out}/icons-sheet.png')
