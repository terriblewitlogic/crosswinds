#!/usr/bin/env python3
"""Prepare the full 41-design set, retaining the six approved bitmaps exactly."""
import hashlib
import json
from pathlib import Path
import numpy as np
from PIL import Image, ImageOps

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT/'assets/source/x4/full-illustrations-v8'
OUT = ROOT/'assets/source/x4/small-illustrations-v8'


def paper_image(path):
    original=Image.open(path).convert('RGBA')
    paper=Image.new('RGBA',original.size,'white');paper.alpha_composite(original)
    return paper.convert('L')


def locate_drawing(sheet, slot):
    """Find the isolated drawing below its label in the observed 2x3 layout."""
    width,height=sheet.size
    x0,x1=(0,width//2) if slot%2==0 else (width//2,width)
    # The guide's three anchors are 260, 600 and 940 on a 1254-pixel page.
    row=slot//2;edges=[0,round(height*.335),round(height*.663),height]
    y0,y1=edges[row:row+2]
    cell=sheet.crop((x0,y0,x1,y1));ink=np.asarray(cell)<168
    occupied=np.flatnonzero(ink.any(axis=1))
    gaps=np.flatnonzero(np.diff(occupied)>=8)
    if not len(gaps):raise ValueError(f'Cannot separate label and drawing in slot {slot}')
    split=max(gaps,key=lambda k:int(occupied[k+1]-occupied[k]))
    label_bottom=int(occupied[split]);drawing_top=int(occupied[split+1])
    if label_bottom-int(occupied[0])>35:
        raise ValueError(f'Ambiguous label region in slot {slot}')
    body=cell.crop((0,drawing_top,cell.width,cell.height))
    bounds=body.point(lambda p:255 if p<168 else 0).getbbox()
    if not bounds:raise ValueError(f'Empty drawing in slot {slot}')
    crop=[x0+bounds[0],y0+drawing_top+bounds[1],x0+bounds[2],y0+drawing_top+bounds[3]]
    if crop[2]-crop[0]>180 or crop[3]-crop[1]>180:
        raise ValueError(f'Unexpected drawing footprint: slot {slot}: {crop}')
    return crop


def main():
    OUT.mkdir(parents=True,exist_ok=True)
    catalog=json.loads((SOURCE/'catalog.json').read_text())
    assert len(catalog)==41
    records=[];sheets={}
    overrides=json.loads((SOURCE/'crop-overrides.json').read_text()) if (SOURCE/'crop-overrides.json').exists() else {}
    for spec in catalog:
        width,height=spec['target'];record=dict(spec)
        if 'inherited' in spec:
            path=ROOT/f"assets/source/x4/small-illustrations-v7/{spec['inherited']}.png"
            result=Image.open(path).convert('L')
            if result.size!=(width,height):raise ValueError(f'Wrong inherited size: {path}')
            record.update(source=str(path.relative_to(ROOT)),operations=['copy approved native pixels'],
                          sourceInkSize=list(result.size),fittedSize=list(result.size),scale=[1,1])
        else:
            path=SOURCE/f"{spec['group']}-generated.png"
            if spec['group'] not in sheets:sheets[spec['group']]=paper_image(path)
            sheet=sheets[spec['group']]
            crop=overrides.get(spec['id'],{}).get('crop') or locate_drawing(sheet,spec['slot'])
            subject=sheet.crop(crop)
            pad=2 if width>48 else 1
            fits=subject.width<=width-pad*2 and subject.height<=height-pad*2
            fitted=subject if fits else ImageOps.contain(subject,(width-pad*2,height-pad*2),Image.Resampling.BOX)
            result=Image.new('L',(width,height),255)
            result.paste(fitted,((width-fitted.width)//2,(height-fitted.height)//2))
            threshold=overrides.get(spec['id'],{}).get('threshold',168)
            result=result.point(lambda p:255 if p>=threshold else 0)
            operations=['crop']+([] if fits else ['BOX resize once before threshold'])+['monochrome threshold','pad']
            record.update(source=str(path.relative_to(ROOT)),sourceCanvas=list(sheet.size),crop=crop,
                          sourceInkSize=list(subject.size),fittedSize=list(fitted.size),threshold=threshold,
                          scale=[fitted.width/subject.width,fitted.height/subject.height],operations=operations)
            subject.save(OUT/f"{spec['id']}-source-crop.png")
        pixels=np.asarray(result)
        if not set(np.unique(pixels)).issubset({0,255}) or not (pixels==0).any():
            raise ValueError(f'Invalid monochrome art: {spec["id"]}')
        if any((edge==0).any() for edge in [pixels[0],pixels[-1],pixels[:,0],pixels[:,-1]]):
            raise ValueError(f'Ink touches slot border: {spec["id"]}')
        record.update(runtimeSize=[width,height],sourceSha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                      sha256=hashlib.sha256(result.tobytes()).hexdigest())
        result.save(OUT/f"{spec['id']}.png");records.append(record)
    (OUT/'manifest.json').write_text(json.dumps(records,indent=2)+'\n')
    new=[r for r in records if 'inherited' not in r]
    print(json.dumps({'total':len(records),'approvedRetained':len(records)-len(new),'new':len(new),
                      'newWithoutResize':sum(r['scale']==[1,1] for r in new),
                      'newWithOneResize':sum(r['scale']!=[1,1] for r in new)}))


if __name__=='__main__':main()
