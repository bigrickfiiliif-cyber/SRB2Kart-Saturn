import json,os,datetime
d=json.load(open('built.json'))
from PIL import Image
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
XA={'Lolita':None,'Danuta Stankiewicz & Lolita':None,'Aziza A.':None,'Bektaş ve Sırtlan':None,'Fist':['Storm'],'FiST':['Storm'],'Caput':['Moldarljós','Human Mutation','Electrosónico'],'Saw':None,'SAW':['Saw Live 12-29-06','A Cut Above'],'Hieronymus':['Save the Day','Alone In This Tree'],'NTS':['6 in a Row']}
d['albums']=[a for a in d['albums'] if not (a['artist'] in XA and (a['title'] in XA[a['artist']] if XA[a['artist']] else not (a['artist']=='Lolita' and a['title']=='Das Mixtape')))]
DROPMB=['Zwei Mann gegen den Rest','Dobermann Demotape Part 1','WWW','Der Teenieslasher','Schwiizerchracher','Und jetz... was hät das mit mir z tue?','Verrückte Ratten','Willkommen Auf St. Pauli','Hokus Pokus','Verdammt nah am Unaussprechlichen','Keine Wärme EP','Blackbook CD 3','G.B.Z.-Oholika II','Gute Diese Mixtape','PS Klasse','Vom Mann in Reno','Die John Bello Story II','Liebling, ich habe das Label geschrumpft','Heimspiel','Live in Stuttgart','Fornika für alle','Dünya Dönüyor (Die Welt dreht sich)','Gesucht Gefunden EP','M.A.D.U. 2 (Mukke aus der Unterschicht)','M.A.D.U. 3 (Mukke aus der Unterschicht)','Zuhältertape, Volume 1','Zuhältertape, Volume 1: X-Mas Edition','Zuhältertape, Volume 3','Boss Der Bosse (Zuhältertape Vol. 2)','Wurzeln & Flügel','Das Buch der drei Ringe','Is’n Schnabelding: Willkommen in Schnabylon','Beat of Life, Volume 1','Return of Hip Hop','Chrigu','Die Jagd auf den König','Momentaufnahmen 2','Momentaufnahmen 3','Momentaufnahmen']
d['albums']=[a for a in d['albums'] if not (a['src']=='mb' and a['title'] in DROPMB)]
MIX=['Die John Bello Story, Vol. 2','Liebling, ich habe das Label geschrumpft (Mit Ali A$ & Tua)','Zuhältertape, Vol. 3','Die Jagd auf den König 1','Die Jagd auf den König 2']
for a in d['albums']:
    if a['title'] in MIX: a['type']='Mixtape'
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
