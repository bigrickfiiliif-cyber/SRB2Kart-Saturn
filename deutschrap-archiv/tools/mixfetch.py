import json,os,urllib.parse
from mbresolve import get
sel=json.load(open('mixsel.json')); R='mixres.json'
res=json.load(open(R)) if os.path.exists(R) else {}
for i,(k,c) in enumerate(sel.items()):
    if k in res: continue
    if c['rel']:
        rel=get('https://musicbrainz.org/ws/2/release/%s?inc=recordings+labels+artist-credits+media&fmt=json'%c['rel'],True)
    else:
        b=get('https://musicbrainz.org/ws/2/release?'+urllib.parse.urlencode({'release-group':k,'inc':'recordings+labels+artist-credits+media','limit':10,'fmt':'json'}),True)
        rels=sorted((b or {}).get('releases',[]),key=lambda x:(x.get('date') or '9999',-sum(len(m.get('tracks',[])) for m in x.get('media',[]))))
        rel=rels[0] if rels else None
    out=dict(c)
    if rel:
        tracks=[]
        for mi,m in enumerate(rel.get('media',[]),1):
            for t in m.get('tracks',[]):
                tracks.append({'d':mi,'n':int(t.get('position') or 0),'t':t['title'],'a':''.join(c2['name']+c2.get('joinphrase','') for c2 in t.get('artist-credit',[])),'ms':t.get('length') or 0})
        labs=[l['label']['name'] for l in rel.get('label-info',[]) if l.get('label')]
        fmts=sorted(set(m.get('format') or '' for m in rel.get('media',[]))-{''})
        out['mbrel']={'id':rel['id'],'label':labs[0] if labs else '','tracks':tracks,'formats':fmts}
    res[k]=out
    if i%10==0: json.dump(res,open(R,'w'),ensure_ascii=False); print(i,flush=True)
json.dump(res,open(R,'w'),ensure_ascii=False); print('done')
