"""Builds the LVGL image and font C files for the EVBikes ride screen (800x480).

Run from this tools folder (needs Python 3 with pillow and numpy):
    EVB_ASSETS=/Users/adesh/EVBikes/assets python3 gen_assets.py
The C files are written to the sketch folder (one level up).
"""
from PIL import Image, ImageFont
import numpy as np, os
from compose import A, shell, hexc
from layout import X0, SX
OUT=os.environ.get('EVB_OUT', '..')
F=A+'fonts/'

def c_bytes(b):
    lines=[]
    for i in range(0,len(b),24):
        lines.append('    '+', '.join(f'0x{v:02x}' for v in b[i:i+24])+',')
    return '\n'.join(lines)

def alpha_of(path, width=None):
    im=Image.open(A+path).convert('RGBA')
    a=im.split()[3]
    if width: a=a.resize((width,im.height),Image.LANCZOS)
    return np.array(a,np.uint8)

def squashed_width(path): return max(1,round(Image.open(A+path).width*SX))

def dither_rgb565(rgb):
    bayer=np.array([[0,8,2,10],[12,4,14,6],[3,11,1,9],[15,7,13,5]],np.float32)/16-0.5
    h,w,_=rgb.shape
    t=np.tile(bayer,(h//4+1,w//4+1))[:h,:w]
    r=np.clip(np.round(rgb[...,0]/255*31+t),0,31).astype(np.uint16)
    g=np.clip(np.round(rgb[...,1]/255*63+t),0,63).astype(np.uint16)
    b=np.clip(np.round(rgb[...,2]/255*31+t),0,31).astype(np.uint16)
    return (r<<11)|(g<<5)|b

class ImageFile:
    def __init__(self,name): self.name=name; self.parts=[]; self.decls=[]
    def add(self,sym,cf,w,h,stride,data):
        self.parts.append(f'''static LV_ATTRIBUTE_LARGE_CONST const uint8_t {sym}_map[] = {{
{c_bytes(data)}
}};

const lv_image_dsc_t {sym} = {{
    .header = {{
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = {cf},
        .flags = 0,
        .w = {w},
        .h = {h},
        .stride = {stride},
    }},
    .data_size = sizeof({sym}_map),
    .data = {sym}_map,
}};
''')
        self.decls.append(f'extern const lv_image_dsc_t {sym}; /* {w}x{h} */')
    def a8(self,sym,a): h,w=a.shape; self.add(sym,'LV_COLOR_FORMAT_A8',w,h,w,a.tobytes())
    def rgb565(self,sym,rgb):
        h,w,_=rgb.shape; px=dither_rgb565(rgb.astype(np.float32))
        self.add(sym,'LV_COLOR_FORMAT_RGB565',w,h,w*2,px.astype('<u2').tobytes())
    def rgb565a8(self,sym,rgb,alpha):
        h,w,_=rgb.shape; px=dither_rgb565(rgb.astype(np.float32))
        self.add(sym,'LV_COLOR_FORMAT_RGB565A8',w,h,w*2,px.astype('<u2').tobytes()+alpha.astype(np.uint8).tobytes())
    def write(self):
        with open(f'{OUT}/{self.name}.c','w') as f:
            f.write('/* Generated from the EVBikes design art. Do not edit by hand. */\n#include "evb_images.h"\n\n'+'\n'.join(self.parts))
        return self.decls

def gradient_fill(alpha, start, end, ramp_from=0.0):
    h,w=alpha.shape; t=np.clip((np.arange(w)/(w-1)-ramp_from)/(1-ramp_from),0,1)
    s=np.array(hexc(start),np.float32); e=np.array(hexc(end),np.float32)
    row=s[None,:]+(e-s)[None,:]*t[:,None]
    return np.repeat(row[None,:,:],h,axis=0)

def build_images():
    decls=[]
    for mode in ('eco','sport'):
        c=shell(mode)[:,X0:X0+1152]
        bg=np.array(Image.fromarray(c.clip(0,255).astype(np.uint8)).resize((800,480),Image.LANCZOS))
        f=ImageFile(f'evb_bg_{mode}'); f.rgb565(f'evb_img_shell_{mode}',bg); decls+=f.write()
    f=ImageFile('evb_images')
    for side in 'lr':
        for i in range(8):
            p=f'cluster/seg_{side}{i}.png'; f.a8(f'evb_img_seg_{side}{i}',alpha_of(p,squashed_width(p)))
    # Speed digits share one crop so every cell lines up.
    x0,y0,x1,y1=39,29,139,151
    for d in [str(i) for i in range(10)]+['dash']:
        f.a8(f'evb_img_speed_{d}',alpha_of(f'design/speed_{d}.png')[y0:y1,x0:x1])
        f.a8(f'evb_img_speed_foot_{d}',alpha_of(f'design/speed_foot_{d}.png')[y0:y1,x0:x1])
    f.a8('evb_img_speed_unit_kph',alpha_of('design/speed_unit_kph.png'))
    f.a8('evb_img_orbit',alpha_of('cluster/orbit.png',200))
    f.a8('evb_img_glow_spot',alpha_of('images/glow_blob_150x40.png'))
    f.a8('evb_img_mode_glow',alpha_of('images/glow_blob_140x64.png'))
    f.a8('evb_img_mode_chip',alpha_of('cluster/chip.png'))
    bike=np.array(Image.open(A+'cluster/bike_180.png').convert('RGBA'))
    f.rgb565a8('evb_img_bike',bike[...,:3],bike[...,3])
    tw=140
    left=alpha_of('cluster/bar_pointed_left.png',tw); right=alpha_of('cluster/bar_pointed_right.png',tw)
    f.a8('evb_img_battery_track',left)
    f.a8('evb_img_temp_track',right)
    f.rgb565a8('evb_img_battery_fill',gradient_fill(left,'#904229','#969683'),left)
    f.rgb565a8('evb_img_battery_fill_low',gradient_fill(left,'#B01E2E','#E0A0A6'),left)
    f.rgb565a8('evb_img_temp_fill',gradient_fill(right,'#91C0A9','#8F432B',108/194),right)
    f.a8('evb_img_temp_ruler',alpha_of('cluster/ruler.png',tw))
    t=np.linspace(0,1,280); fade=np.clip(np.minimum(t/0.3,(1-t)/0.3),0,1)
    f.a8('evb_img_fade_line',(fade*255+0.5).astype(np.uint8)[None,:])
    for n in ('left','high_beam','low_beam','warning','abs','battery','right'):
        f.a8(f'evb_img_tt_{n}',alpha_of(f'design/tt_{n}.png'))
    for sym,p in (('bluetooth','icons/28/bluetooth.png'),('signal','icons/22/signal_full.png'),('battery_bolt','icons/26/battery_bolt.png'),
                  ('thermo','icons/28/thermo.png'),('compass','icons/36/compass.png'),('bell','icons/24/bell.png'),
                  ('mute','icons/24/mute.png'),('settings','icons/24/settings.png')):
        f.a8(f'evb_img_icon_{sym}',alpha_of(p))
    decls+=f.write()
    with open(f'{OUT}/evb_images.h','w') as h:
        h.write('/* Generated from the EVBikes design art. Do not edit by hand. */\n#pragma once\n\n#include <lvgl.h>\n\n#ifdef __cplusplus\nextern "C" {\n#endif\n\n'
                +'\n'.join(decls)+'\n\n#ifdef __cplusplus\n}\n#endif\n')

FONTS=[  # symbol, ttf, px, characters
 ('evb_font_label_12','Inter-BoldItalic.ttf',12,'MAXAMPRPM×0123456789 '),
 ('evb_font_label_16','Inter-BoldItalic.ttf',16,'RANGEOD '),
 ('evb_font_label_19','Inter-BoldItalic.ttf',19,'TRIPSOR '),
 ('evb_font_mode_26','Inter-BoldItalic.ttf',26,'ECO'),
 ('evb_font_value_30','Inter-BoldItalic.ttf',30,'0123456789-'),
 ('evb_font_italic_14','Inter-Italic.ttf',14,'0123456789-%'),
 ('evb_font_italic_16','Inter-Italic.ttf',16,'0123456789-°ckm'),
 ('evb_font_italic_18','Inter-Italic.ttf',18,'0123456789-'),
 ('evb_font_regular_15','Inter-Regular.ttf',15,'CF'),
 ('evb_font_regular_16','Inter-Regular.ttf',16,'apm'),
 ('evb_font_regular_18','Inter-Regular.ttf',18,'RPND°'),
 ('evb_font_regular_22','Inter-Regular.ttf',22,'0123456789:-'),
 ('evb_font_regular_24','Inter-Regular.ttf',24,'0123456789-'),
 ('evb_font_regular_26','Inter-Regular.ttf',26,'RPND'),
]

def build_font(sym, ttf, px, chars):
    font=ImageFont.truetype(F+ttf,px)
    ascent,descent=font.getmetrics()
    chars=sorted(set(chars))
    bitmap=bytearray(); glyphs=['    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,']
    for ch in chars:
        adv=round(font.getlength(ch)*16)
        mask,(ox,oy)=font.getmask2(ch,mode='L')
        a=np.array(mask,np.uint8).reshape(mask.size[1],mask.size[0]) if mask.size[0]*mask.size[1] else np.zeros((0,0),np.uint8)
        if a.size and a.any():
            ys,xs=np.nonzero(a); a=a[ys.min():ys.max()+1,xs.min():xs.max()+1]; ox+=xs.min(); oy+=ys.min()
            h,w=a.shape
            q=((a.astype(np.int32)*15+127)//255).flatten()
            if len(q)%2: q=np.append(q,0)
            start=len(bitmap); bitmap+=bytes(((q[0::2]<<4)|q[1::2]).astype(np.uint8))
            ofs_y=ascent-(oy+h)
            glyphs.append(f'    {{.bitmap_index = {start}, .adv_w = {adv}, .box_w = {w}, .box_h = {h}, .ofs_x = {ox}, .ofs_y = {ofs_y}}}, /* {ch!r} */')
        else:
            glyphs.append(f'    {{.bitmap_index = {len(bitmap)}, .adv_w = {adv}, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0}}, /* {ch!r} */')
    first=ord(chars[0]); offsets=[ord(c)-first for c in chars]
    cap=font.getbbox('H',anchor='ls'); xh=font.getbbox('x',anchor='ls')
    return f'''/* {sym}: {ttf} {px}px, characters "{''.join(chars)}". Generated, do not edit by hand. */
static LV_ATTRIBUTE_LARGE_CONST const uint8_t {sym}_bitmap[] = {{
{c_bytes(bitmap) if bitmap else '    0x00,'}
}};

static const lv_font_fmt_txt_glyph_dsc_t {sym}_glyph_dsc[] = {{
{chr(10).join(glyphs)}
}};

static const uint16_t {sym}_unicode_list[] = {{
    {', '.join(str(o) for o in offsets)}
}};

static const lv_font_fmt_txt_cmap_t {sym}_cmaps[] = {{
    {{
        .range_start = {first},
        .range_length = {offsets[-1]+1},
        .glyph_id_start = 1,
        .unicode_list = {sym}_unicode_list,
        .glyph_id_ofs_list = NULL,
        .list_length = {len(chars)},
        .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY,
    }},
}};

static const lv_font_fmt_txt_dsc_t {sym}_dsc = {{
    .glyph_bitmap = {sym}_bitmap,
    .glyph_dsc = {sym}_glyph_dsc,
    .cmaps = {sym}_cmaps,
    .kern_dsc = NULL,
    .kern_scale = 0,
    .cmap_num = 1,
    .bpp = 4,
    .kern_classes = 0,
    .bitmap_format = 0,
}};

const lv_font_t {sym} = {{
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,
    .line_height = {ascent+descent},
    .base_line = {descent},
#if LV_VERSION_CHECK(9, 6, 0) || LVGL_VERSION_MAJOR >= 10
    .cap_height = {-cap[1]},
    .x_height = {-xh[1]},
#endif
    .subpx = LV_FONT_SUBPX_NONE,
    .underline_position = -1,
    .underline_thickness = 1,
#if LV_VERSION_CHECK(9, 3, 0)
    .static_bitmap = 1,
#endif
    .dsc = &{sym}_dsc,
    .fallback = NULL,
    .user_data = NULL,
}};
'''

def build_fonts():
    body=['/* Inter glyphs used by the EVBikes ride screen. Generated, do not edit by hand. */\n#include "evb_fonts.h"\n']
    decls=[]
    for sym,ttf,px,chars in FONTS:
        body.append(build_font(sym,ttf,px,chars)); decls.append(f'extern const lv_font_t {sym};')
    open(f'{OUT}/evb_fonts.c','w').write('\n'.join(body))
    open(f'{OUT}/evb_fonts.h','w').write('/* Inter glyphs used by the EVBikes ride screen. Generated, do not edit by hand. */\n#pragma once\n\n#include <lvgl.h>\n\n#ifdef __cplusplus\nextern "C" {\n#endif\n\n'+'\n'.join(decls)+'\n\n#ifdef __cplusplus\n}\n#endif\n')

if __name__=='__main__':
    os.makedirs(OUT,exist_ok=True); build_images(); build_fonts(); print('done')
