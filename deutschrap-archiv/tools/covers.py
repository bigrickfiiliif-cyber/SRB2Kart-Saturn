import json,os,urllib.request,io,time,sys
from PIL import Image
d=json.load(open('built.json')); os.makedirs('out/covers',exist_ok=True)
for a in d['albums']:
    if a.get('src')!='itunes' or not a.get('_art'): continue
    p=f"out/covers/{a['_cid']}.jpg"
    if os.path.exists(p): continue
    u=a['_art'].replace('100x100bb','300x300bb')
    for k in range(4):
        try:
            b=urllib.request.urlopen(u,timeout=30).read(); im=Image.open(io.BytesIO(b)).convert('RGB').resize((240,240),Image.LANCZOS)
            im.save(p,'JPEG',quality=72,optimize=True,progressive=True); break
        except Exception as e: print('ERR',e,u,file=sys.stderr); time.sleep(5)
print('done',len(os.listdir('out/covers')))
