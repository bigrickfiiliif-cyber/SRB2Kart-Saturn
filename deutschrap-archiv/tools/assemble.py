import json,os,datetime
d=json.load(open('built.json'))
for a in d['albums']:
    cid=a.pop('_cid',None); a.pop('_art',None)
    a['cover']=f'covers/{cid}.jpg' if cid and os.path.exists(f'out/covers/{cid}.jpg') else ''
    for t in a['tracks']:
        if not t['f']: del t['f']
for a in d['artists']:
    if not a['apple']: del a['apple']
d['slugOf']={a['name']:a['slug'] for a in d['artists']}
d['built']=datetime.date.today().strftime('%d.%m.%Y')
s=json.dumps(d,ensure_ascii=False,separators=(',',':')).replace('</','<\\/')
html=open('template.html').read().replace('/*DATA*/',s)
open('out/index.html','w').write(html)
print('html',len(html)//1024,'KB covers',len(os.listdir('out/covers')),sum(1 for a in d['albums'] if a['cover']),'/',len(d['albums']))
