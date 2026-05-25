import json, pathlib
p=pathlib.Path(r"C:\Users\xtrem\Downloads\CPlusPlus\CAN CTRL\Kuksa-vss-data\vss_rel_4.0.json") 
data=json.loads(p.read_text(encoding="utf-8")) 
p.write_text(json.dumps(data, indent=2, ensure_ascii=False)+"\n", encoding="utf-8")
print("Formatted", p)