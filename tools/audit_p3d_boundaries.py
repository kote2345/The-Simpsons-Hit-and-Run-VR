"""Read-only structural audit of uncompressed P3D files in an installed game."""
import argparse
import collections
import json
import struct
from pathlib import Path

def inspect(path):
    data=path.read_bytes()
    issues=[]
    if len(data)<12:return ['short file header']
    magic=struct.unpack_from('<I',data)[0]
    if magic!=0xff443350:return ['compressed or other format: not decoded']
    def walk(begin,end,depth):
        if depth>32:
            issues.append('nesting exceeds 32');return
        cursor=begin
        while end-cursor>=12:
            kind,payload,total=struct.unpack_from('<III',data,cursor)
            if payload<12 or total<payload or total>end-cursor:
                issues.append(f'invalid chunk at {cursor}: {kind:x}/{payload}/{total}');return
            if total>payload:walk(cursor+payload,cursor+total,depth+1)
            cursor+=total
        if cursor!=end:issues.append(f'{end-cursor} trailing bytes at {cursor}')
    walk(0,len(data),0)
    return issues

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root',type=Path)
    args=parser.parse_args()
    result={};count=0;classes=collections.Counter()
    for path in args.root.rglob('*.p3d'):
        count+=1
        problems=inspect(path)
        if problems:
            result[str(path.relative_to(args.root))]=problems
            for problem in problems:classes[problem.split(':')[0]]+=1
    print(json.dumps({'files':count,'issues':result,'summary':classes},indent=2))
