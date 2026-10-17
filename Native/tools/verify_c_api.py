# SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
# Copyright (c) 2026 Алексей Савельев (Alex Merqury). See ../../LICENSE.
"""Load the actual DLL through a foreign-function interface, without C# or Unity."""
import argparse,ctypes as c,json,math
from pathlib import Path
parser=argparse.ArgumentParser();parser.add_argument('--dll',required=True);parser.add_argument('--output',default=str(Path(__file__).resolve().parents[1]/'results/C_ABI.json'));args=parser.parse_args()
dll=c.CDLL(str(Path(args.dll).resolve()));checks=0
def require(condition,name):
    global checks
    if not condition:raise AssertionError(name)
    checks+=1
def bind(name,returns,*params):
    f=getattr(dll,name);f.restype=returns;f.argtypes=list(params);return f
class Movement(c.Structure):
    _fields_=[(n,c.c_int32) for n in ['accepted','features','skipped','evidence','rebased']]+[('violations',c.c_uint32),('signal',c.c_int32)]+[(n,c.c_double) for n in ['speed','acceleration','vertical','prediction','observed','allowed','risk']]
create=bind('out_guard_movement_create',c.c_void_p,c.c_void_p);destroy=bind('out_guard_movement_destroy',None,c.c_void_p)
observe=bind('out_guard_movement_observe',c.c_int,c.c_void_p,*([c.c_double]*4),c.c_int,c.POINTER(Movement))
abi=bind('out_native_abi',c.c_int);require(abi()==1,'C ABI version');require(c.sizeof(Movement)==88,'Movement ABI layout')
tracker=create(None)
try:
    require(bool(tracker),'Movement create');result=Movement()
    require(observe(tracker,0,0,0,0,0,c.byref(result))==1 and result.accepted==1,'Trusted initial pose')
    require(observe(tracker,.1,100,100,0,0,c.byref(result))==1 and not result.accepted and result.violations&1 and result.violations&4,'Movement evidence through DLL')
    require(observe(None,.2,0,0,0,0,c.byref(result))==0 and not result.accepted,'Null handle cannot leave accepted output')
finally:destroy(tracker)
key=(c.c_uint8*32)(*range(32));codec_create=bind('out_guard_codec_create',c.c_void_p,c.c_void_p,c.c_size_t);codec_destroy=bind('out_guard_codec_destroy',None,c.c_void_p)
encode=bind('out_guard_codec_encode',c.c_int,c.c_void_p,c.c_void_p,c.c_size_t,c.c_char_p,c.c_void_p,c.c_size_t,c.POINTER(c.c_size_t))
decode=bind('out_guard_codec_decode',c.c_int,*encode.argtypes)
codec=codec_create(key,32)
try:
    require(bool(codec),'Codec create');payload=c.create_string_buffer(b'a\x00z');written=c.c_size_t()
    require(encode(codec,payload,3,b'slot/a',None,0,c.byref(written))==2 and written.value==88,'Encode buffer negotiation')
    encoded=(c.c_uint8*written.value)();require(encode(codec,payload,3,b'slot/a',encoded,len(encoded),c.byref(written))==1,'Encode actual envelope')
    output=(c.c_uint8*16)();require(decode(codec,encoded,len(encoded),b'slot/a',output,len(output),c.byref(written))==1 and bytes(output[:written.value])==b'a\x00z','Decode native envelope')
    require(decode(codec,encoded,len(encoded),b'slot/b',output,len(output),c.byref(written))==0 and written.value==0,'Wrong context clears valid output length')
    require(not codec_create(key,31),'Invalid key size rejected')
finally:codec_destroy(codec)
FP=c.POINTER(c.c_float);IP=c.POINTER(c.c_int32);BP=c.POINTER(c.c_uint8)
class Bank(c.Structure):_fields_=[('poses',FP),('field',FP),('mass',FP),('mirrored',IP),('parents',IP),('response',BP),('gain',FP),('damping',FP)]
edge_a=[0,1,2,2,4,5,2,7,8,0,10,11,12,0,14,15,16];edge_b=[1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17]
parents=[-1]*18
for a,b in zip(edge_a,edge_b):parents[b]=a
poses=[]
for p in range(17):
    for j in range(18):
        if j<4:v=[0,.95+j*.24,0]
        elif j<10:
            k=(j-4)%3;side=-1 if j<7 else 1;v=[side*(.2+k*.2),1.45-k*.2,.02*k]
        else:
            k=(j-10)%4;side=-1 if j<14 else 1;v=[side*.13,[.92,.5,.085,.055][k],.18 if k==3 else .02 if k==1 else 0]
        poses+=v
pose_array=(c.c_float*len(poses))(*poses);field_array=(c.c_float*81)(*[value for z in range(3) for y in range(3) for x in range(3) for value in [-(x-1)*1.2,-(y-1)*1.2,-(z-1)*1.2]])
mass=(c.c_float*18)(*([1]*18));mirrored=(c.c_int32*18)(*range(18));parent_array=(c.c_int32*18)(*parents);response=(c.c_uint8*128)(*([0x44]*128));gain=(c.c_float*16)(*([.6]*16));damping=(c.c_float*16)(*([1.4]*16));bank=Bank(pose_array,field_array,mass,mirrored,parent_array,response,gain,damping)
world_create=bind('out_motor_world_create',c.c_void_p,c.POINTER(Bank),c.c_int);world_destroy=bind('out_motor_world_destroy',None,c.c_void_p);tick=bind('out_motor_world_tick',c.c_int,c.c_void_p)
workers=bind('out_motor_world_workers',c.c_int,c.c_void_p,c.c_int);locomotion=bind('out_motor_world_locomotion',c.c_int,c.c_void_p,c.c_int,c.c_float,c.c_float,c.c_float)
positions=bind('out_motor_world_copy_positions',c.c_int,c.c_void_p,c.c_void_p,c.c_size_t,c.POINTER(c.c_size_t))
world=world_create(c.byref(bank),4)
try:
    require(bool(world),'Native Motor world created through C ABI');require(workers(world,4)==1,'Native workers configured');require(locomotion(world,0,0,1,.55)==1,'Native locomotion command')
    require(locomotion(world,99,0,1,.55)==0,'Actor index rejected without exception crossing ABI')
    for _ in range(30):require(tick(world)==1,'Full native world tick')
    required=c.c_size_t();require(positions(world,None,0,c.byref(required))==2 and required.value==4*18*3,'Position output negotiation');output=(c.c_float*required.value)();require(positions(world,output,len(output),c.byref(required))==1 and all(math.isfinite(v) for v in output),'Finite particle output')
finally:world_destroy(world)
require(not world_create(None,4),'Missing paid data view rejected');require(not world_create(c.byref(bank),-1),'Invalid population rejected')
out=Path(args.output);out.parent.mkdir(parents=True,exist_ok=True);out.write_bytes((json.dumps({'format':'OUT-NATIVE-C-ABI-1','assertions':checks,'passed':True,'scope':'Actual Windows x64 DLL loaded using Python ctypes; synthetic fixture, no Unity editor or paid bank.'},indent=2)+'\n').encode('utf-8'))
print(f'PASS {checks} DLL C-ABI assertions')
