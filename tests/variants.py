from pathlib import Path
import json,subprocess,time
from PIL import Image,ImageFilter
root=Path(__file__).resolve().parents[1]
folder=root/'out/testdata'
image=Image.open(folder/'id-front.jpg');rows=[]
for angle in [90,180,270]:image.rotate(angle,expand=True).save(folder/f'rotation-{angle}.png')
image.filter(ImageFilter.GaussianBlur(1.5)).save(folder/'blur.png')
image.crop((280,260,700,600)).save(folder/'missing-edge.png')
Image.new('RGB',(800,800),'white').save(folder/'blank.png')
(folder/'corrupt.jpg').write_bytes(b'not a jpeg');(folder/'empty.jpg').write_bytes(b'')
for name,kind,side,expected_code in [(f'rotation-{a}.png','id_card','portrait',0) for a in [90,180,270]]+[
 ('blur.png','id_card','portrait',0),('missing-edge.png','id_card','portrait',0),('blank.png','id_card','portrait',0),
 ('id-front.jpg','driver_license','main',0),('corrupt.jpg','id_card','portrait',1),('empty.jpg','id_card','portrait',1),
 ('id-front.jpg','id_card','main',1),('id-front.jpg','unknown','portrait',1)]:
 p=subprocess.run([str(root/'out/build/native/cardlens-ocr'),str(root/'models'),str(folder/name),kind,side],capture_output=True,text=True)
 assert p.returncode==expected_code,(name,p.returncode,p.stderr)
 result=json.loads(p.stdout) if p.returncode==0 else {}
 if name.startswith('rotation-'):assert result['fields']['idNumber']['value']=='440305199505132561',(name,result)
 if name=='blank.png':assert result['status']=='failed' and 'LOW_SHARPNESS' in result['warnings']
 if kind=='driver_license':assert result['status']=='failed' and 'DOCUMENT_TYPE_MISMATCH' in result['warnings']
 for field in list(result.get('fields',{}).values())+result.get('lines',[]):
  for x,y in field['polygon']:assert 0<=x<result['width'] and 0<=y<result['height'],(name,x,y)
 rows.append({'case':name,'type':kind,'exit_code':p.returncode,'status':result.get('status'),'warnings':result.get('warnings',[]),'passed':True})
(root/'out/validation/variants.json').write_text(json.dumps(rows,indent=2)+'\n');print(json.dumps(rows,indent=2))
