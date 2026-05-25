import json, pathlib; 
p3 = pathlib.Path(r"C:\\Users\\xtrem\\Downloads\\CPlusPlus\\CAN CTRL\\Kuksa-vss-data\\vss_rel_4.0.json")
p4 = pathlib.Path(r"C:\\Users\\xtrem\\Downloads\\CPlusPlus\\CAN CTRL\\Kuksa-vss-data\\vss_rel_4.1.json")
d = json.loads(p3.read_text(encoding='utf-8'))
d2 = json.loads(p4.read_text(encoding='utf-8'))
changes = []

def cmp(a,b,path):
    if isinstance(a, dict) and isinstance(b, dict):
        for k in sorted(set(a) - set(b)): changes.append((path+[k],'removed',a[k],None))
        for k in sorted(set(b) - set(a)): changes.append((path+[k],'added',None,b[k]))
        for k in sorted(set(a) & set(b)): cmp(a[k],b[k],path+[k])
    elif isinstance(a, list) and isinstance(b, list):
        if a == b: return
        if len(a) != len(b): changes.append((path,'list_length_changed',len(a),len(b)))
        for i,(x,y) in enumerate(zip(a,b)): cmp(x,y,path+[f'[{i}]'])
        if len(a) < len(b):
            for i in range(len(a),len(b)): changes.append((path+[f'[{i}]'],'added',None,b[i]))
        elif len(a) > len(b):
            for i in range(len(b),len(a)): changes.append((path+[f'[{i}]'],'removed',a[i],None))
    else:
        if a != b: changes.append((path,'changed',a,b))

cmp(d,d2,[])
print('3.0 size:', p3.stat().st_size, 'bytes')
print('4.1 size:', p4.stat().st_size, 'bytes')
print('Total diffs:', len(changes))
for i,(path,kind,old,new) in enumerate(changes[:200],1):
    path_str='.'.join(str(p) for p in path)
    if kind=='removed': print(f'{i}. REMOVED {path_str}')
    elif kind=='added': print(f'{i}. ADDED   {path_str}')
    elif kind=='list_length_changed': print(f'{i}. LIST_LEN {path_str}: {old} -> {new}')
    elif kind=='changed': print(f'{i}. CHANGED {path_str}: {old!r} -> {new!r}')
