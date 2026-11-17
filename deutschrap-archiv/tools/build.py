import json,re,os,unicodedata,datetime
import common
from common import norm,select,base_title
common.EXCL.update(norm(x.split('|')[0])+'|'+norm(x.split('|')[1]) for x in open('exclude.txt') if '|' in x)
raw=json.load(open('raw.json')); tr=json.load(open('tracks.json')) if os.path.exists('tracks.json') else {}
seeds={l.split('|')[0]:l.strip().split('|') for l in open('artists.txt') if l.strip() and not l.startswith('#')}
ALIAS={'Joka31':'Joka','Aci_Krank':'Aci Krank','ACI Krank':'Aci Krank','G.P.C':'GPC','G.P.C.':'GPC','MB 1000':'MB1000','JokA':'Joka','CHUKY':'Chuky','SicknezzOne':'Sicknezz','Abu Sex':'Abusex','Deso Dogg aka Soleil':'Deso Dogg','Scheusal und Kunstfehler':'Scheusal & Kunstfehler','Akte One':'AKTEone','AkteOne':'AKTEone','Fresh Wask':'Freshwask','BTM SQUAD':'BTM Squad','Creaturen Der Nacht':'Creaturen der Nacht','MOR':'M.O.R.','Nabil M':'Nabil M.','Sady K':'Sady K.','$ady K':'Sady K.','Saw':'SAW','Blockwart und BasuR':'Blockwart & BasuR','Fist':'FiST','Inferno79':'Inferno.79','Shiml One':'Shiml','Flowin IMMO':'Flowin Immo','Deine Lieblingsrapper':'Deine Lieblings Rapper','MAdoppelT':'MaDoppelT','Boba Fettt':'Boba Fettt','Roey Marquis II.':'Roey Marquis II','DJ Ilan Hosted By Sonny Black & Saad':'DJ Ilan','King Kool Savas':'Kool Savas','Absolute Beginner':'Beginner','Cora E':'Cora E.','Laas Unltd':'Laas Unltd.','Fanta 4':'Die Fantastischen Vier','Die Fantastischen 4':'Die Fantastischen Vier','Sido & B-Tight':'Sido & B-Tight','Sonny Black':'Bushido','Saad':'Baba Saad','Savas':'Kool Savas','Moe Mitchel':'Moe Mitchell','Jonesman':'Jonesmann','KIZ':'K.I.Z','K. I. Z':'K.I.Z','Frank White':'Fler','Buddy':'Buddy','MC Ferris':'Ferris MC','Luut&Tüütli':'Luut & Tüütli','Toni L':'Toni-L','Raf Camora':'RAF Camora','Kaas':'KAAS','Kitty Kat':'Kitty Kat','Eko':'Eko Fresh'}
COMPOUND=[s for s in seeds if re.search(r' & | und ',s)]+['Frauenarzt & Manny Marc','Sonny Black & Frank White','Kool Savas & Azad','Bushido & Baba Saad','Kollegah & Farid Bang']
KEEP_TOGETHER=[s for s in seeds if re.search(r' & | und ',s)]
def canon(n):
    n=n.strip().strip('.,;:"').strip()
    n=re.sub(r'\s+',' ',n)
    return ALIAS.get(n,n)
def split_names(s):
    s=re.sub(r'(?i)\s+(?:hosted by|mixed by:?|&|and|und|x|vs\.?|feat\.?|featuring|ft\.?)\s+','|',' '+s+' ').strip()
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

from common import year_of,typ_of
raw['artists']={k:v for k,v in raw['artists'].items() if k in seeds}
albums=[]
def from_itunes(r,seedname,year=None,typ=None,title=None):
    seed=seeds[seedname]; artist=r['artistName']; arts=split_names(artist)
    a={'id':'i'+str(r['collectionId']),'title':title or base_title(r['collectionName']),'artist':artist,'artists':arts,'year':year or year_of(r),'type':typ or typ_of(r),
       'land':seed[1],'label':label_of(r.get('copyright','')),'genres':seed[2].split(','),'producers':[],'guests':[],'note':'',
       'apple':r['collectionViewUrl'].split('?')[0],'src':'itunes','_art':r.get('artworkUrl100'),'_cid':str(r['collectionId']),'_ry':int(r.get('releaseDate','0000')[:4] or 0)}
    tl=[parse_track(t,arts) for t in sorted(tr.get(a['_cid'],[]),key=lambda t:(t['d'],t['n']))]
    a['tracks']=tl; return a
for r in select(raw): albums.append(from_itunes(r,r['_seed'],r['_year'],r['_typ'],r['_title']))
ids={a['id'] for a in albums}
def same(a,title,names):
    if norm(base_title(a['title']))!=norm(base_title(title)): return False
    na=norm(a['artist']); return any(n and (n in na or na in n) for n in names)
# ---- MusicBrainz merge ----
mbres=json.load(open('mbres.json')) if os.path.exists('mbres.json') else {}
MBT={'Album':'Album','EP':'EP'}
mixres=json.load(open('mixres.json')) if os.path.exists('mixres.json') else {}
for rg in list(mbres.values())+list(mixres.values()):
    if rg['seed'] not in seeds: continue
    names=[norm(rg['seed']),norm(rg['credit'])]
    typ='Mixtape' if 'Mixtape/Street' in rg['sec'] else 'Live' if 'Live' in rg['sec'] else MBT[rg['ptype']]
    hit=[a for a in albums if same(a,rg['title'],names)]
    if hit:
        for a in hit:
            a['mb']=rg['rg']
            if rg['year']<a['year'] or (a.get('_ry',0)>2009):
                if a.get('_ry',0)>rg['year']: a['late']=a['_ry']
                a['year']=rg['year']
            if a['type'] in ('Album','EP','EP?'): a['type']=typ
        continue
    if rg.get('itunes'):
        x=rg['itunes']
        if 'i'+str(x['collectionId']) in ids: continue
        a=from_itunes(x,rg['seed'],rg['year'],typ,rg['title']); a['mb']=rg['rg']
        if a['_ry']>rg['year']: a['late']=a['_ry']
        albums.append(a); ids.add(a['id']); continue
    m=rg.get('mbrel')
    if not m: continue
    arts=split_names(rg['credit']) if rg['credit'] not in KEEP_TOGETHER else [rg['credit']]
    seed=seeds[rg['seed']]
    tl=[parse_track(t,arts) for t in sorted(m['tracks'],key=lambda t:(t['d'],t['n']))]
    albums.append({'id':'mb-'+rg['rg'][:8],'title':rg['title'],'artist':rg['credit'],'artists':arts,'year':rg['year'],'type':typ,'land':rg.get('land') or seed[1],
      'label':m['label'],'genres':seed[2].split(','),'producers':[],'guests':[],'note':'','apple':'','src':'mb','formats':m['formats'],'tracks':tl,'mb':rg['rg']})
# ---- manual knowledge over everything ----
for m in manual:
    names=[norm(x) for x in [m[0]]+m[0].split(' & ')]
    hit=[a for a in albums if same(a,m[1],names)]
    if not hit:
        arts=split_names(m[0]) if m[0] not in KEEP_TOGETHER else [m[0]]
        sid=re.sub(r'[^a-z0-9]+','-',unicodedata.normalize('NFKD',(m[0]+' '+m[1]).lower()).encode('ascii','ignore').decode()).strip('-')
        a={'id':'m-'+sid,'title':m[1],'artist':m[0],'artists':arts,'year':int(m[2]),'type':m[3] or 'Album','land':seeds.get(arts[0],[None,'DE'])[1],'label':m[4],
           'genres':[],'producers':[],'guests':[],'note':'','apple':'','src':'manual','tracks':[]}
        albums.append(a); hit=[a]
    for a in hit:
        if a['src'] in ('itunes','mb') and not a.get('mb'):
            y=int(m[2])
            if 2000<=y<=2009: a['year']=y
        if m[3] and m[3]!='Album': a['type']=m[3]
        if m[4] and not a['label'] or a['src']=='manual': a['label']=m[4] or a['label']
        if m[5]: a['genres']=m[5].split(',')
        a['producers']=[canon(x) for x in m[6].split(',') if x.strip()]
        if len(m)>8 and m[8]: a['note']=m[8]
# ---- EP? resolution, instrumentals, notes ----
songs={}
for a in albums:
    if a['type'] not in ('EP?',):
        for n in a['artists']: songs.setdefault(n,set()).update(norm(t['t']) for t in a['tracks'])
keep=[]
for a in albums:
    if not 2000<=a['year']<=2009: continue
    if a['type']=='EP?':
        if any(norm(a['title']) in songs.get(n,()) for n in a['artists']) or len(a['tracks'])<4: continue
        a['type']='EP'
    if a['type']=='Instrumental' or re.search(r'instrumental',a['title'],re.I):
        a['type']='Instrumental'
        if 'Instrumental' not in a['genres']: a['genres']=['Instrumental']+a['genres']
    if a.get('late'): a['note']=(a['note']+' ' if a['note'] else '')+f"Ursprünglich {a['year']} erschienen, digital erst {a['late']}."
    if a['src']=='mb':
        f=', '.join(a.get('formats') or [])
        a['note']=(a['note']+' ' if a['note'] else '')+'Nicht im iTunes-Katalog gefunden'+(f' (erschienen als {f})' if f else '')+'. Angaben aus MusicBrainz.'
    a['guests']=list(dict.fromkeys(n for t in a['tracks'] for n in t['f']))
    for k in ('_ry','formats'): a.pop(k,None)
    keep.append(a)
albums=keep
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
