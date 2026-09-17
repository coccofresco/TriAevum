"""Build a small live adapter and compare unchanged game runs; never package it."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys


def select_roots(qualification, requested):
    passed={int(e,16) for e in qualification['passed_roots']}
    roots={int(e,16) for e in requested} if requested else passed
    if not roots or not roots <= passed or any(not 0<e<=0xFFFFFFFF or e%4 for e in roots):
        raise ValueError('Live roots must be a nonempty subset of matching replay roots')
    return [f'{e:08X}' for e in sorted(roots)]


def main():
    p=argparse.ArgumentParser(__doc__)
    for name in ('compiler','support','include','baseline','candidate','qualification',
                 'executable','invocation','output'):
        p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--root',action='append')
    p.add_argument('--abba',action='store_true')
    args=p.parse_args()
    roots=select_roots(json.loads(args.qualification.read_text()),args.root)
    args.output.mkdir(parents=True,exist_ok=False)
    args.output=args.output.resolve()
    here=Path(__file__).resolve().parent
    repo=here.parents[2];runtime=repo/'tools/oot3d/native_game_runtime';a32=repo/'tools/oot3d/native_a32_runtime'
    source=here/'aot_live_cohort_probe.cpp';obj=args.output/'live.obj';dll=args.output/'live.dll'
    commands=[[str(args.compiler),'/nologo','/c','/MT','/O2','/EHsc','/std:c++20','/DNOMINMAX','/D_CRT_SECURE_NO_WARNINGS',
               *(f'/I{x}' for x in (runtime,a32,a32/'upstream',args.include)),str(source),f'/Fo{obj}'],
              [str(args.compiler),'/nologo','/LD','/MT',str(obj),str(args.support),f'/Fe{dll}',
               '-fuse-ld=lld','/link','/NOIMPLIB','/threads:2']]
    for cmd in commands:subprocess.run(cmd,check=True,timeout=120)
    env=dict(os.environ,TRIAEVUM_INVOCATION_BASELINE=str(args.baseline.resolve()),
             TRIAEVUM_LIVE_CANDIDATE=str(args.candidate.resolve()),TRIAEVUM_LIVE_ROOTS=','.join(roots),
             TRIAEVUM_LIVE_REPORT='live-cohort.json')
    env.pop('TRIAEVUM_FAMILY_ORACLE',None)
    files=[source,dll,args.baseline,args.candidate,args.support,args.invocation,args.qualification,args.executable]
    (args.output/'provenance.json').write_text(json.dumps({'developer_only':True,'live_execution':True,
        'roots':roots,'commands':commands,'hashes':{str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in files}},indent=2))
    results=[]
    for i,variant in enumerate(['baseline','candidate','candidate','baseline'] if args.abba else ['baseline','candidate']):
        dest=args.output/f'{i}-{variant}'
        subprocess.run([sys.executable,str(here/'measure_prebackend.py'),'--executable',str(args.executable),
            '--invocation',str(args.invocation),'--diagnostic-plugin',str(dll if variant=='candidate' else args.baseline),
            '--frames','360','--runs','1','--output',str(dest)],env=env,check=True,timeout=150)
        data=json.loads((dest/'0/runtime.json').read_text())
        fingerprints={k:data[k] for k in ('memory_content_fingerprint','memory_state_fingerprint','process_state_fingerprint')}
        if results and fingerprints!=results[0]['fingerprints']:
            raise RuntimeError(f'Live game diverged: {dest}')
        counters=json.loads((dest/'0/live-cohort.json').read_text()) if variant=='candidate' else {}
        if variant=='candidate' and not counters['candidate_calls']:
            raise RuntimeError('No live candidate calls; do not credit fallback')
        result={'variant':variant,'fingerprints':fingerprints,'counters':counters,
                'pre_nri_ms':data['benchmark_window']['pre_backend_cpu']['mean_ms']}
        results.append(result)
        (args.output/'results.json').write_text(json.dumps(results,indent=2))
        print(json.dumps(result),flush=True)


if __name__=='__main__':main()
