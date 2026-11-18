import json,os,datetime
d=json.load(open('built.json'))
from PIL import Image
import zlib,math
def synth(i): return 9000000000000+zlib.crc32(i.encode())
DZ=json.load(open('dz.json')) if os.path.exists('dz.json') else {'al':{},'ar':{}}
RT=json.load(open('mbrate.json')) if os.path.exists('mbrate.json') else {'rg':{},'artist':{}}
for a in d['albums']:
    if not a.get('_cid') and os.path.exists(f"out/covers/{synth(a['id'])}.jpg"): a['_cid']=synth(a['id']); a['csrc']='ext'
    z=DZ['al'].get(a['id'])
    if z: a['dz']=z['id']; a['fans']=z.get('fans') or 0
    if a.get('mb') and a['mb'] in RT['rg']: a['r']=RT['rg'][a['mb']]
mx=max([a.get('fans',0) for a in d['albums']]+[1])
for a in d['albums']:
    if 'fans' in a: a['pop']=round(100*math.log10(1+a['fans'])/math.log10(1+mx))
for x in d['artists']:
    z=DZ['ar'].get(x['name'])
    if z: x['fans']=z.get('fans') or 0; x['dz']=z['id']
    if x['name'] in RT['artist']: x['r']=RT['artist'][x['name']]
G=8;S=200
cids=sorted({str(a['_cid']) for a in d['albums'] if a.get('_cid') and os.path.exists(f"out/covers/{a['_cid']}.jpg")})
os.makedirs('out/sprites',exist_ok=True)
for f in os.listdir('out/sprites'): os.remove('out/sprites/'+f)
SPR={}
for n in range(0,len(cids),G*G):
    sh=Image.new('RGB',(G*S,G*S),(30,30,30)); k=n//(G*G)
    for j,c in enumerate(cids[n:n+G*G]):
        im=Image.open(f'out/covers/{c}.jpg').convert('RGB').resize((S,S),Image.LANCZOS)
        sh.paste(im,((j%G)*S,(j//G)*S)); SPR[c]=[k,j]
    sh.save(f'out/sprites/s{k}.jpg',quality=74,optimize=True,progressive=True)
SPR={int(k) if k.isdigit() else k:v for k,v in SPR.items()}
d['albums']=[a for a in d['albums'] if 'brooklyn soul' not in a['title'].lower()]
from common import norm
# manual duplicate fixes: MusicBrainz copies of releases already in the catalogue
for _a in d['albums']:
    if len(_a['artists'])==1: _a['artist']=_a['artists'][0]
# --- AOTY-style genres: primary from artist seed, secondaries from MusicBrainz community genres + seed ---
MBG={}
if os.path.exists('mb.json'):
    for _n,_x in json.load(open('mb.json'))['artists'].items():
        for rg in (_x or {}).get('rgs',[]):
            gl=sorted(rg.get('genres',[]),key=lambda g:-g.get('count',0))
            if gl: MBG[rg['id']]=[g['name'] for g in gl]
            r=rg.get('rating') or {}
            if r.get('votes-count'): RT['rg'][rg['id']]=[r['value'],r['votes-count']]
SKIP={'hip hop','rap','german hip hop','hip-hop','deutschrap','pop','electronic dance music','german rap','swiss hip hop','austrian hip hop'}
def gname(n):
    fix={'hip house':'Hip House','r&b':'R&B','idm':'IDM','edm':'EDM','uk garage':'UK Garage','neue deutsche welle':'Neue Deutsche Welle','lo-fi hip hop':'Lo-Fi Hip Hop','synth-pop':'Synthpop','trip hop':'Trip Hop','alternative r&b':'Alternative R&B','post-punk':'Post-Punk','neo-psychedelia':'Neo-Psychedelia','neo soul':'Neo-Soul'}
    return fix.get(n) or ' '.join(w if w.isupper() else w.capitalize() for w in n.split(' '))
for _a in d['albums']:
    seedg=[g for g in _a['genres'] if g!='Instrumental']
    if _a['type']=='Instrumental' and 'Instrumental Hip Hop' not in seedg: seedg=seedg[:1]+['Instrumental Hip Hop']+seedg[1:]
    extra=[gname(n) for n in MBG.get(_a.get('mb'),[]) if n not in SKIP]
    prim=seedg[0] if seedg else (extra[0] if extra else 'Experimental Hip Hop')
    sec=[]
    for g in extra+seedg[1:]:
        if g!=prim and g not in sec: sec.append(g)
    _a['genres']=[prim]+sec[:4]
    if _a.get('mb') in RT['rg']: _a['r']=RT['rg'][_a['mb']]
DROPART={'Pain','EMP','Einervonuns','Einervonuns / Com.Pare','Bonez MC & RAF Camora feat. Trettmann'}
d['albums']=[x for x in d['albums'] if x['artist'] not in DROPART]
DROPT={'Bossen & Bumsen','KitschKrieg, Vol. 2','KitschKrieg, Vol. 3','Gumbo II - Lost Tapes (2010-2011)','41ER E1NS EP','LETZTER TANZ EP','autismus x autotune','Twilight Zone','Tango & Cash','Rap Knowledge & Bullshit','Eros'}
d['albums']=[x for x in d['albums'] if not (x['src']=='mb' and x['title'] in DROPT)]
# dedupe (title may contain artist name / quotes); prefer iTunes over MusicBrainz-only
pri={'itunes':0,'manual':1,'mb':2}
seen={};drop=set()
for a in sorted(d['albums'],key=lambda a:pri.get(a['src'],3)):
    for art in a['artists']:
        t=norm(a['title']).replace(norm(art),'') or norm(a['title'])
        k=(norm(art),t,a['type']=='Live')
        if k in seen and seen[k]!=a['id']:
            b=next(x for x in d['albums'] if x['id']==seen[k])
            if a.get('late') or (a.get('year',9999)<b.get('year',9999)):
                b['year']=min(a['year'],b['year'])
            drop.add(a['id']);break
        seen[k]=a['id']
d['albums']=[a for a in d['albums'] if a['id'] not in drop]
ids={a['id'] for a in d['albums']}
for x in d['artists']:
    x['main']=[i for i in x['main'] if i in ids]
    x['feats']=[f for f in x['feats'] if f['id'] in ids]
    x['prod']=[i for i in x['prod'] if (i['id'] if isinstance(i,dict) else i) in ids]
# split duo names into their members' pages
byname={x['name']:x for x in d['artists']}
for a in d['albums']:
    new=[]
    for n in a['artists']:
        parts=[p.strip() for p in n.replace(' und ',' & ').split(' & ')]
        if len(parts)>1 and all(p in byname for p in parts):
            for p in parts:
                if a['id'] not in byname[p]['main']: byname[p]['main'].append(a['id'])
                new.append(p)
        else: new.append(n)
    a['artists']=list(dict.fromkeys(new))
d['artists']=[x for x in d['artists'] if x['main'] or x['feats'] or x['prod']]
print('dropped dupes',len(drop))
for a in d['albums']:
    cid=a.pop('_cid',None); a.pop('_art',None)
    a["cover"]=SPR.get(int(cid),"") if cid else ""
    for t in a['tracks']:
        if not t['f']: del t['f']
for a in d['artists']:
    if not a['apple']: del a['apple']
d['slugOf']={a['name']:a['slug'] for a in d['artists']}
d['built']=datetime.date.today().strftime('%d.%m.%Y')
s=json.dumps(d,ensure_ascii=False,separators=(',',':')).replace('</','<\\/')
html=open('template.html').read().replace('/*DATA*/',s)
open('out/index.html','w').write(html)
print('sprites',len(os.listdir('out/sprites')),sum(os.path.getsize('out/sprites/'+f) for f in os.listdir('out/sprites'))//1024,'KB');print('html',len(html)//1024,'KB covers',len(os.listdir('out/covers')),sum(1 for a in d['albums'] if a['cover']),'/',len(d['albums']))
