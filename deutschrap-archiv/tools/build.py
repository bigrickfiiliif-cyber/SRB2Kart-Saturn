import json,re,os,unicodedata,datetime
import common
from common import norm,select,base_title
common.EXCL.update(norm(x.split('|')[0])+'|'+norm(x.split('|')[1]) for x in open('exclude.txt') if '|' in x)
raw=json.load(open('raw.json')); tr=json.load(open('tracks.json')) if os.path.exists('tracks.json') else {}
seeds={l.split('|')[0]:l.strip().split('|') for l in open('artists.txt') if l.strip() and not l.startswith('#')}
ALIAS={'King Kool Savas':'Kool Savas','Absolute Beginner':'Beginner','Cora E':'Cora E.','Laas Unltd':'Laas Unltd.','Fanta 4':'Die Fantastischen Vier','Die Fantastischen 4':'Die Fantastischen Vier','Sido & B-Tight':'Sido & B-Tight','Sonny Black':'Bushido','Saad':'Baba Saad','Savas':'Kool Savas','Moe Mitchel':'Moe Mitchell','Jonesman':'Jonesmann','KIZ':'K.I.Z','K. I. Z':'K.I.Z','Frank White':'Fler','Buddy':'Buddy','MC Ferris':'Ferris MC','Luut&Tüütli':'Luut & Tüütli','Toni L':'Toni-L','Raf Camora':'RAF Camora','Kaas':'KAAS','Kitty Kat':'Kitty Kat','Eko':'Eko Fresh'}
COMPOUND=[s for s in seeds if re.search(r' & | und ',s)]+['Frauenarzt & Manny Marc','Sonny Black & Frank White','Kool Savas & Azad','Bushido & Baba Saad','Kollegah & Farid Bang']
KEEP_TOGETHER=[s for s in seeds if re.search(r' & | und ',s)]
def canon(n):
    n=n.strip().strip('.,;:"').strip()
    n=re.sub(r'\s+',' ',n)
    return ALIAS.get(n,n)
def split_names(s):
    s=re.sub(r'\s+(?:&|and|und|x|vs\.?|feat\.?|featuring|ft\.?)\s+','|',' '+s+' ').strip()
    # restore compounds
    out=[]
    parts=[p for p in re.split(r'\||,\s*|\s*/\s*',s) if p.strip()]
    i=0
    while i<len(parts):
        if i+1<len(parts):
            for joiner in (' & ',' und '):
                c=parts[i].strip()+joiner+parts[i+1].strip()
                if c in KEEP_TOGETHER: out.append(c); i+=2; break
            else:
                out.append(parts[i]); i+=1
            continue
        out.append(parts[i]); i+=1
    return [canon(x) for x in out if canon(x) and len(canon(x))<40]
FEAT=re.compile(r'\s*[\(\[]\s*(?:feat\.?|featuring|ft\.?|mit|w/)\s+([^\)\]]+)[\)\]]',re.I)
FEAT2=re.compile(r'\s+(?:feat\.?|featuring|ft\.)\s+(.+)$',re.I)
def parse_track(t,album_artists):
    title=t['t']; f=[]
    m=FEAT.search(title)
    if m: f+=split_names(m.group(1)); title=FEAT.sub('',title,count=1)
    else:
        m=FEAT2.search(title)
        if m: f+=split_names(m.group(1)); title=title[:m.start()]
    # track artist differing from album artist
    for n in split_names(t['a']):
        if n not in album_artists and n not in f and not any(x.lower() in n.lower() for x in album_artists): f.append(n)
    f=[x for x in dict.fromkeys(f) if x not in album_artists and not re.search(r'remix|version|intro|outro|skit|instrumental',x,re.I)]
    return {'d':t['d'],'n':t['n'],'t':title.strip(),'ms':t['ms'],'f':f}
def label_of(c):
    c=re.sub(r'^\s*[℗©Ⓟ]\s*','',c or ''); c=re.sub(r'^\s*(?:\(P\)\s*)?\d{4}(?:\s*[-,/]\s*\d{4})?\s*','',c)
    c=re.sub(r'^\s*[℗©]\s*','',c).strip(' ,.')
    c=re.sub(r'\s*(GmbH|Ltd\.?|Inc\.?|AG|Germany|Deutschland)(?=\s|$).*$','',c).strip(' ,.-')
    c=re.sub(r',.*$','',c)
    c=re.sub(r'\s+under (exclusive )?licen[cs]e.*$','',c,flags=re.I)
    c=re.sub(r'\s*(distributed|marketed|vertrieben).*$','',c,flags=re.I)
    if re.search(r'copyright|recording|rights|all rights',c,re.I) and len(c)>30: c=''
    return c[:40].strip()
manual=[l.rstrip('\n').split('|') for l in open('manual.txt') if l.strip() and not l.startswith('#')]
mused=set()
def find_manual(artist,title):
    for i,m in enumerate(manual):
        if norm(base_title(m[1]))==norm(title) and (norm(m[0]) in norm(artist) or norm(artist) in norm(m[0]) or norm(artist.split(' & ')[0]) in norm(m[0])):
            return i,m
    return None,None
raw['artists']={k:v for k,v in raw['artists'].items() if k in seeds}
albums=[]
for r in select(raw):
    seed=seeds[r['_seed']]
    artist=r['artistName']
    arts=split_names(artist)
    mi,m=find_manual(artist,r['_title'])
    genres=seed[2].split(',')
    a={'id':'i'+str(r['collectionId']),'title':r['_title'],'artist':artist,'artists':arts,'year':r['_year'],'type':r['_typ'],
       'land':seed[1],'label':label_of(r.get('copyright','')),'genres':genres,'producers':[],'guests':[],'note':'',
       'apple':r['collectionViewUrl'].split('?')[0],'src':'itunes','_art':r.get('artworkUrl100'),'_cid':str(r['collectionId'])}
    if m:
        mused.add(mi)
        y=int(m[2]); 
        if 2000<=y<=2009: a['year']=y
        if m[3]: a['type']=m[3]
        if m[4]: a['label']=m[4]
        if m[5]: a['genres']=m[5].split(',')
        a['producers']=[canon(x) for x in m[6].split(',') if x.strip()]
        a['note']=m[8] if len(m)>8 else ''
    tl=[parse_track(t,arts) for t in sorted(tr.get(a['_cid'],[]),key=lambda t:(t['d'],t['n']))]
    a['tracks']=tl
    a['guests']=list(dict.fromkeys(n for t in tl for n in t['f']))
    albums.append(a)
for i,m in enumerate(manual):
    if i in mused: continue
    arts=split_names(m[0]) if m[0] not in KEEP_TOGETHER else [m[0]]
    land=seeds.get(arts[0],[None,'DE'])[1]
    sid=re.sub(r'[^a-z0-9]+','-',unicodedata.normalize('NFKD',(m[0]+' '+m[1]).lower()).encode('ascii','ignore').decode()).strip('-')
    if any(x['id']=='m-'+sid for x in albums): continue
    albums.append({'id':'m-'+sid,'title':m[1],'artist':m[0],'artists':arts,'year':int(m[2]),'type':m[3] or 'Album','land':land,'label':m[4],
      'genres':m[5].split(','),'producers':[canon(x) for x in m[6].split(',') if x.strip()],'guests':[],'note':m[8] if len(m)>8 else '','apple':'','src':'manual','tracks':[]})
# artists index
AR={}
def ar(n):
    if n not in AR:
        s=seeds.get(n)
        slug=re.sub(r'[^a-z0-9]+','-',unicodedata.normalize('NFKD',n.lower()).encode('ascii','ignore').decode()).strip('-') or 'x'
        AR[n]={'name':n,'slug':slug,'land':s[1] if s else '','main':[],'feats':[],'prod':[],
               'apple':(raw['artists'].get(n,{}).get('artistId') and f"https://music.apple.com/de/artist/{raw['artists'][n]['artistId']}") or ''}
    return AR[n]
for a in albums:
    for n in a['artists']: ar(n)['main'].append(a['id'])
    for p in a['producers']: ar(p)['prod'].append(a['id'])
    for t in a['tracks']:
        for n in t['f']: ar(n)['feats'].append({'id':a['id'],'t':t['t']})
# fill land for compound artists from first member
for n,x in AR.items():
    if not x['land']:
        for a in albums:
            if n in a['artists']: x['land']=a['land']; break
slugs={}
for x in AR.values():
    b=x['slug'];k=2
    while x['slug'] in slugs: x['slug']=f'{b}-{k}';k+=1
    slugs[x['slug']]=x['name']
    x['letter']=(lambda c: c if c.isalpha() and c.isascii() else '0-9')(unicodedata.normalize('NFKD',x['name'][0].upper()).encode('ascii','ignore').decode()[:1] or '#')
json.dump({'albums':albums,'artists':list(AR.values())},open('built.json','w'),ensure_ascii=False)
print(len(albums),'albums',len(AR),'artists',sum(1 for a in albums if a['src']=='manual'),'manual-only',sum(len(a['tracks']) for a in albums),'tracks')
