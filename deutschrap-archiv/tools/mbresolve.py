# for every MB release group 2000-2009 (Album/EP) find iTunes version or fetch MB tracklist
import json,time,urllib.request,urllib.parse,os,sys,re
from common import norm,base_title
UA={'User-Agent':'DeutschrapArchiv/1.0 (fan archive)'}
mb=json.load(open('mb.json')); R='mbres.json'
res=json.load(open(R)) if os.path.exists(R) else {}
seeds={l.split('|')[0]:l.strip().split('|') for l in open('artists.txt') if l.strip() and not l.startswith('#')}
BAD={'Compilation','Remix','DJ-mix','Interview','Spokenword','Audiobook','Audio drama','Field recording'}
def credit(rg): return ''.join(c['name']+c.get('joinphrase','') for c in rg.get('artist-credit',[])) or ''
def cands():
    seen={}
    for name,a in mb['artists'].items():
        if not a: continue
        for rg in a['rgs']:
            d=rg.get('first-release-date') or ''
            if not d[:4].isdigit() or not 2000<=int(d[:4])<=2009: continue
            if rg.get('primary-type') not in ('Album','EP'): continue
            if set(rg.get('secondary-types',[]))&BAD: continue
            seen.setdefault(rg['id'],dict(rg=rg['id'],title=rg['title'],credit=credit(rg) or a['name'],year=int(d[:4]),date=d,ptype=rg['primary-type'],sec=rg.get('secondary-types',[]),seed=name))
    return list(seen.values())
def get(url,mbreq=False):
    for k in range(10):
        try:
            req=urllib.request.Request(url,headers=UA if mbreq else {})
            with urllib.request.urlopen(req,timeout=30) as r: time.sleep(1.25 if mbreq else 1.0); return json.load(r)
        except Exception as e: print('ERR',e,url[:90],file=sys.stderr); time.sleep(min(60,6*(k+1)))
    return None
if __name__=='__main__':
    C=cands()
    have=json.load(open('built.json'))['albums']
    def matched(c):
        ns=[norm(c['seed']),norm(c['credit'])]
        for a in have:
            if norm(base_title(a['title']))==norm(base_title(c['title'])) and any(n in norm(a['artist']) or norm(a['artist']) in n for n in ns): return True
        return False
    for c in C:
        if c['rg'] not in res and matched(c): res[c['rg']]=dict(c,known=True)
    print(len(C),'release groups',sum(1 for c in C if c['rg'] not in res),'to resolve'); sys.stdout.flush()
    for i,c in enumerate(C):
        if c['rg'] in res: continue
        land=seeds[c['seed']][1]; out=dict(c)
        q=urllib.parse.urlencode({'term':c['credit'].split(' & ')[0]+' '+c['title'],'entity':'album','country':{'DE':'de','AT':'at','CH':'ch'}[land],'limit':25})
        r=get('https://itunes.apple.com/search?'+q) or {'results':[]}
        hit=None
        for x in r['results']:
            if norm(base_title(x['collectionName']))==norm(c['title']) and (norm(c['seed']) in norm(x['artistName']) or norm(x['artistName']) in norm(c['credit'])):
                hit=x; break
        if hit: out['itunes']=hit; out['itc']={'DE':'de','AT':'at','CH':'ch'}[land]
        else:
            b=get('https://musicbrainz.org/ws/2/release?'+urllib.parse.urlencode({'release-group':c['rg'],'inc':'recordings+labels+artist-credits+media','limit':10,'fmt':'json'}),True)
            if b and b.get('releases'):
                rels=sorted(b['releases'],key=lambda x:(x.get('date') or '9999',-sum(len(m.get('tracks',[])) for m in x.get('media',[]))))
                rel=rels[0]
                tracks=[]
                for mi,m in enumerate(rel.get('media',[]),1):
                    for t in m.get('tracks',[]):
                        tracks.append({'d':mi,'n':int(t.get('position') or 0),'t':t['title'],'a':''.join(c2['name']+c2.get('joinphrase','') for c2 in t.get('artist-credit',[])),'ms':t.get('length') or 0})
                labs=[l['label']['name'] for l in rel.get('label-info',[]) if l.get('label')]
                fmts=sorted(set(m.get('format') or '' for m in rel.get('media',[]))-{''})
                out['mbrel']={'id':rel['id'],'label':labs[0] if labs else '','tracks':tracks,'formats':fmts,'cover':rel.get('cover-art-archive',{}).get('front',False)}
        res[c['rg']]=out
        if i%10==0: json.dump(res,open(R,'w'),ensure_ascii=False); print(i)
    json.dump(res,open(R,'w'),ensure_ascii=False); print('done')
