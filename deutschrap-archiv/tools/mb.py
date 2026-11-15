import json,time,urllib.request,urllib.parse,os,sys,re
from common import norm
UA={'User-Agent':'DeutschrapArchiv/1.0 (fan archive)'}
C='mb.json'; db=json.load(open(C)) if os.path.exists(C) else {'artists':{},'rels':{}}
def get(path,**q):
    q['fmt']='json'; url='https://musicbrainz.org/ws/2/'+path+'?'+urllib.parse.urlencode(q)
    for k in range(10):
        try:
            with urllib.request.urlopen(urllib.request.Request(url,headers=UA),timeout=30) as r:
                time.sleep(1.25); return json.load(r)
        except Exception as e:
            print('ERR',e,url[:100],file=sys.stderr); time.sleep(min(60,6*(k+1)))
    return None
def save(): json.dump(db,open(C,'w'),ensure_ascii=False)
AREAS={'Germany','Austria','Switzerland','Berlin','Hamburg','Vienna','Zürich','Bern','München','Munich','Frankfurt am Main','Köln','Cologne','Stuttgart','Düsseldorf','Bonn','Basel','Linz','Graz','Essen','Dortmund','Hannover','Mannheim','Heidelberg','Bremen','Leipzig','Dresden','Offenbach am Main','Wuppertal','Bochum','Gelsenkirchen','Kassel','Nürnberg','Kiel','Augsburg','Rostock','Luzern','St. Gallen','Salzburg','Innsbruck','Braunschweig','Bielefeld','Aachen','Wiesbaden','Mainz','Darmstadt','Karlsruhe','Freiburg im Breisgau','Saarbrücken','Halle (Saale)','Potsdam','Magdeburg','Erfurt','Chemnitz','Lübeck','Gießen','Neuss','Duisburg','Recklinghausen','Hagen','Witten','Solingen','Bergisch Gladbach'}
def ok(a):
    if a.get('country') in ('DE','AT','CH'): return True
    if a.get('area',{}).get('name') in AREAS or a.get('begin-area',{}).get('name') in AREAS: return True
    d=(a.get('disambiguation') or '').lower()
    return any(w in d for w in ('german','austria','swiss','deutsch','rapper','hip hop','hip-hop','berlin','hamburg','wien','zürich','bern'))
seeds=[l.strip().split('|') for l in open('artists.txt') if l.strip() and not l.startswith('#')]
for name,land,g in seeds:
    if name in db['artists']: continue
    r=get('artist/',query=f'artist:"{name}"',limit=10)
    if r is None: continue
    cands=[a for a in r.get('artists',[]) if norm(a['name'])==norm(name) and ok(a)]
    if not cands:
        cands=[a for a in r.get('artists',[]) if any(norm(x.get('name',''))==norm(name) for x in a.get('aliases',[])) and ok(a)]
    if not cands: db['artists'][name]=None; print('MISS',name); save(); continue
    a=cands[0]; rgs=[]; off=0
    while True:
        b=get('release-group',artist=a['id'],limit=100,offset=off,inc='artist-credits')
        if not b: break
        rgs+=b['release-groups']; off+=100
        if off>=b.get('release-group-count',0): break
    db['artists'][name]={'id':a['id'],'name':a['name'],'rgs':rgs}
    print('OK',name,len(rgs)); save()
