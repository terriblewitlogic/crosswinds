#!/usr/bin/env python3
"""Recover/generated art at 1:1: crop, monochrome conversion, padding; NO resize."""
from pathlib import Path
import hashlib
import json
import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'assets/source/x4/native-size-v9'


def paper(path):
    rgba = Image.open(path).convert('RGBA')
    bg = Image.new('RGBA', rgba.size, 'white')
    bg.alpha_composite(rgba)
    return bg.convert('L')


def bounds(image):
    return image.point(lambda p: 255 if p < 168 else 0).getbbox()


def group_cuts(values, count):
    occupied = np.flatnonzero(values)
    gaps = sorted(range(len(occupied)-1), key=lambda i: occupied[i+1]-occupied[i], reverse=True)[:count-1]
    if any(occupied[i+1]-occupied[i] < 8 for i in gaps):
        raise ValueError('Drawing groups are not isolated')
    return [0] + sorted((int(occupied[i])+int(occupied[i+1]))//2 for i in gaps) + [len(values)]


def sheet_cells(path, cols, rows, labels=False):
    sheet = paper(path)
    if labels:
        # Reference sheets use a fixed grid with labels separated above art.
        cuts_x = [round(sheet.width*i/cols) for i in range(cols+1)]
        cuts_y = [round(sheet.height*i/rows) for i in range(rows+1)]
    else:
        ink = np.asarray(sheet) < 168
        cuts_x = group_cuts(ink.any(axis=0), cols)
        cuts_y = group_cuts(ink.any(axis=1), rows)
    result=[]
    for row in range(rows):
        for col in range(cols):
            x,y=cuts_x[col],cuts_y[row]
            cell=sheet.crop((x,y,cuts_x[col+1],cuts_y[row+1]))
            top=0
            if labels:
                occupied=np.flatnonzero((np.asarray(cell)<168).any(axis=1))
                gaps=np.flatnonzero(np.diff(occupied)>=8)
                if not len(gaps): raise ValueError(f'Label missing from {path}, {row},{col}')
                gap=max(gaps,key=lambda i:int(occupied[i+1]-occupied[i]))
                if occupied[gap]-occupied[0]>22: raise ValueError('Ambiguous drawing/label boundary')
                top=int(occupied[gap+1]); cell=cell.crop((0,top,cell.width,cell.height))
            box=bounds(cell)
            if not box: raise ValueError(f'Empty drawing in {path}')
            result.append((cell.crop(box),[x+box[0],y+top+box[1],x+box[2],y+top+box[3]]))
    return result


def main():
    OUT.mkdir(parents=True,exist_ok=True)
    records=[]
    def add(name, source, crop, subject, slot):
        if subject.width+2>slot[0] or subject.height+2>slot[1]:
            raise ValueError(f'{name}: {subject.size} exceeds {slot}; regenerate smaller, never resize')
        bw=subject.point(lambda p: 0 if p<168 else 255)
        result=Image.new('L',(bw.width+2,bw.height+2),255);result.paste(bw,(1,1))
        result.save(OUT/(name.lower()+'.png'))
        records.append(dict(name=name,source=str(source.relative_to(ROOT)),crop=crop,
                            sourceInkSize=list(subject.size),nativeSize=list(result.size),slot=slot,
                            operations=['crop','threshold 168','1px transparent-exterior padding'],
                            resized=False,sourceSha256=hashlib.sha256(source.read_bytes()).hexdigest()))
    old=json.loads((ROOT/'assets/source/x4/small-illustrations-v8/manifest.json').read_text())
    inherited={r['id']:r for r in json.loads((ROOT/'assets/source/x4/small-illustrations-v7/manifest.json').read_text())}
    for spec in old:
        family=spec['family']
        if family not in ('good','landmark','facility'):continue
        rec=inherited[spec['inherited']] if 'inherited' in spec else spec
        source=ROOT/rec['source'];crop=rec['crop'];subject=paper(source).crop(crop)
        box=bounds(subject);subject=subject.crop(box)
        crop=[crop[0]+box[0],crop[1]+box[1],crop[0]+box[2],crop[1]+box[3]]
        slot={'good':(72,72),'landmark':(192,168),'facility':(100,84)}[family]
        add(f'{family.upper()}_DETAIL_{spec["index"]}',source,crop,subject,slot)
    for file,prefix,cols,rows,labels,slot in [
        ('dock-source.png','DOCK_STAGE',2,2,True,(160,128)),
        ('captains-source.png','CAPTAIN_DETAIL',4,4,True,(96,112)),
    ]:
        source=OUT/file
        for i,(subject,crop) in enumerate(sheet_cells(source,cols,rows,labels)):
            add(f'{prefix}_{i}',source,crop,subject,slot)
    # Functional chart symbols are drawn at their final resolution. The imagegen
    # attempts exceeded this footprint, so none are reduced into unreadable art.
    for kind in range(5):
        symbol=Image.new('L',(30,24),255);d=ImageDraw.Draw(symbol)
        d.polygon([(1,18),(28,18),(23,22),(6,22)],fill=0)
        if kind<2:
            for mast in ([13] if kind==0 else [9,20]):
                d.line((mast,2,mast,18),fill=0)
                d.polygon([(mast-1,4),(mast-1,16),(max(2,mast-8),16)],fill=255,outline=0)
                d.polygon([(mast+1,5),(min(27,mast+7),16),(mast+1,16)],fill=255,outline=0)
        else:
            for mast in ([8,21] if kind==2 else [6,15,24]):
                d.line((mast,1,mast,18),fill=0)
                d.polygon([(mast-3,4),(mast+3,4),(mast+4,9),(mast-4,9)],fill=255,outline=0)
                d.polygon([(mast-3,11),(mast+3,11),(mast+4,16),(mast-4,16)],fill=255,outline=0)
            if kind==4: d.rectangle((2,16,6,19),fill=0)
        source=OUT/f'map-symbol-source-{kind}.png';symbol.save(source)
        add(f'MAP_SHIP_{kind}',source,[0,0,30,24],symbol,(32,26))
        records[-1]['method']='native chart geometry; no image resampling'
    (OUT/'manifest.json').write_text(json.dumps(records,indent=2)+'\n')
    print(f'Prepared {len(records)} native variants without resizing')


if __name__=='__main__':main()
