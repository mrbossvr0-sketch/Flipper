"""Static mockup of the 128x64 monochrome UI implemented in private_lab.c.
Preview isn't an emulator or hardware screenshot.
"""
from PIL import Image, ImageDraw, ImageFont
from pathlib import Path

out = Path(__file__).parent
root = Path('/usr/share/fonts/truetype/dejavu')
small = ImageFont.truetype(str(root/'DejaVuSansMono.ttf'), 8)
medium = ImageFont.truetype(str(root/'DejaVuSansMono-Bold.ttf'), 9)
large = ImageFont.truetype(str(root/'DejaVuSansMono-Bold.ttf'), 12)

def canvas(title):
    im = Image.new('1',(128,64),1)
    d=ImageDraw.Draw(im)
    d.rectangle((0,0,127,12), fill=0)
    d.text((3,2),title,font=small,fill=1)
    d.text((105,2),'86%',font=small,fill=1)
    return im,d

def home():
    im,d=canvas('PRIVATE / LAB 3.0')
    d.rectangle((2,16,125,41),outline=0)
    d.text((8,20),'AUTHORIZED TEST',font=medium,fill=0)
    d.text((8,31),'RX-only diagnostics',font=small,fill=0)
    d.rectangle((3,45,42,55),fill=0)
    d.text((7,46),'TOOLS',font=small,fill=1)
    d.rectangle((45,45,84,55),outline=0)
    d.text((49,46),'STATUS',font=small,fill=0)
    d.rectangle((87,45,125,55),outline=0)
    d.text((91,46),'GUIDE',font=small,fill=0)
    d.line((3,57,42,57),fill=0)
    d.text((22,57),'<   OK to open   >',font=small,fill=0)
    return im

def rf():
    im,d=canvas('RF METER / RX')
    d.text((4,16),'433.920 MHz',font=medium,fill=0)
    d.text((86,16),'-57 dBm',font=small,fill=0)
    d.rectangle((4,27,123,36),outline=0)
    d.rectangle((6,29,99,34),fill=0)
    d.text((5,38),'Peak -48 dBm  LOCK',font=small,fill=0)
    xs=[0,2,3,5,2,7,3,5,6,1,2,3,7,8,5,4,3,8,6,4,5,2,8,3,7,4,4,6,6,4,2,3,4,8,7,8,4,4,6,3,5,8]
    for i,h in enumerate(xs):d.line((11+i*2,54,11+i*2,54-h),fill=0)
    d.line((0,56,127,56),fill=0)
    d.text((3,57),'<> FREQ  ^ SWP   OK SAVE',font=small,fill=0)
    return im

def settings():
    im,d=canvas('SETTINGS / STORED')
    rows=[('RF FREQUENCY','433MHz'),('AUTO SWEEP','OFF'),('RSSI ALERT','ON'),('ALERT LEVEL','-70dBm')]
    for i,(l,r) in enumerate(rows):
        y=15+i*10
        if i==0:d.rectangle((3,y,124,y+9),fill=0)
        fg=1 if i==0 else 0
        d.text((6,y+1),l,font=small,fill=fg)
        d.text((123,y+1),r,font=small,fill=fg,anchor='ra')
    d.line((0,56,127,56),fill=0)
    d.text((3,57),'<> CHANGE   OK SELECT',font=small,fill=0)
    return im

cards=[('Home dashboard',home()),('Passive RF meter',rf()),('Saved settings',settings())]
scale=4
W=128*scale+36; H=64*scale+78
montage=Image.new('RGB',(3*W+30,H+40),(239,242,246))
d=ImageDraw.Draw(montage)
headline=ImageFont.truetype(str(root/'DejaVuSans-Bold.ttf'),18)
label=ImageFont.truetype(str(root/'DejaVuSans.ttf'),13)
d.text((20,11),'PRIVATE LAB 3.0   /   Flipper Zero interface preview',font=headline,fill=(28,38,54))
for i,(title,im) in enumerate(cards):
    x=15+i*W; y=55
    d.rounded_rectangle((x,y,x+W-18,y+H-7),radius=14,fill=(255,255,255),outline=(206,213,222),width=2)
    d.text((x+16,y+12),title,font=label,fill=(29,45,64))
    big=im.convert('RGB').resize((128*scale,64*scale),Image.Resampling.NEAREST)
    big=Image.eval(big, lambda p:255 if p>0 else 0)
    montage.paste(big,(x+16,y+36))
montage.save(out/'ui_preview.png')
for title,im in cards:
    im.convert('RGB').resize((512,256),Image.Resampling.NEAREST).save(out/(title.lower().replace(' ','_')+'.png'))
