"""Build a private, closed direct-call family; never rebuild/cache the full title."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time

REPO = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO / 'tools/oot3d/native_a32_runtime'))
import whole_aot_cpp as aot
from analyze_aot_region_surface import memory_observer


def closure(program, root, maximum):
    functions = {f['entry']: f for f in program['functions']}
    found, active = set(), set()
    def visit(entry):
        if entry in active:
            raise ValueError('Recursive family')
        if entry in found:
            return
        f = functions[entry]
        if (not f['closed_static_cfg'] or f['indirect_sites'] or
                f['unresolved_static_edges'] or not f['blocks']):
            raise ValueError(f'Open family at {entry:08X}')
        found.add(entry)
        if len(found) > maximum:
            raise ValueError('Family exceeds build budget')
        active.add(entry)
        for c in f['direct_calls'] + f['tail_calls']:
            visit(c['target'])
        active.remove(entry)
    visit(root)
    return sorted(found)


def cohort(program, roots, maximum):
    if not roots:
        raise ValueError('At least one family root is required')
    entries = set()
    for root in sorted(set(roots)):
        entries.update(closure(program, root, maximum))
        if len(entries) > maximum:
            raise ValueError('Cohort exceeds build budget')
    return sorted(entries)


def main():
    p = argparse.ArgumentParser(__doc__)
    for name in ('program', 'code', 'compiler', 'support', 'include', 'output'):
        p.add_argument('--'+name, type=Path, required=True)
    p.add_argument('--root', type=lambda x: int(x,16), action='append', default=[])
    p.add_argument('--selection', type=Path, help='Hashed selection from select_aot_cohort.py')
    p.add_argument('--optimized', action='store_true')
    p.add_argument('--coverage', action='store_true', help='Instrument function entries; not for timing')
    p.add_argument('--max-functions', type=int, default=16)
    args = p.parse_args()
    program = json.loads(args.program.read_text())
    code = args.code.read_bytes()
    if hashlib.sha256(code).hexdigest() != program['code_sha256']:
        raise ValueError('Code/program mismatch')
    if args.selection:
        selection = json.loads(args.selection.read_text())
        if (selection['program_sha256'] != hashlib.sha256(args.program.read_bytes()).hexdigest()
                or selection['code_sha256'] != program['code_sha256']):
            raise ValueError('Selection input mismatch')
        if args.root:
            raise ValueError('Use either selection or explicit roots')
        args.root = [int(e,16) for e in selection['roots']]
    roots = sorted(set(args.root))
    entries = cohort(program,roots,args.max_functions)
    if args.selection and entries != sorted(int(e,16) for e in selection['functions']):
        raise ValueError('Selection closure mismatch')
    functions, external = aot._load_functions(program, {
        'format': aot.SELECTION_FORMAT, 'functions': [{'entry':e} for e in entries]}, code)
    if external:
        raise ValueError('External owners are not qualified for this experiment')
    for f in functions:
        if f.entry in {0x0036C174,0x00372224,0x00307BD8}:
            raise ValueError('Family crosses an existing native external owner')
        if any(memory_observer(i.raw,True) for b in f.blocks for i in b.instructions):
            raise ValueError(f'Memory/system observer in {f.entry:08X}')
        if args.optimized and f.entry == 0x002BFCB4:
            begin=f.entry-program['base']
            expected='bf19b62569a5f5f4b6c92f3755a74621f2e80a129113f9696e90fd2e319e09fe'
            if hashlib.sha256(code[begin:begin+192]).hexdigest()!=expected:
                raise ValueError('Unqualified native triangle body; address alone is insufficient')
    symbols = {f.entry:aot._symbol(f.name,f.entry) for f in functions}
    lines = ['bool FamilySupportsRoot(uint32_t pc) { return ' +
             ' || '.join(f'pc==0x{e:08X}U' for e in roots) + '; }',
             f'constexpr bool kFamilyOptimized={str(args.optimized).lower()};']
    lines += ['constexpr uint32_t kFamilyEntries[]={' + ','.join(f'0x{f.entry:08X}U' for f in functions) + '};',
              f'uint64_t familyVisits[{len(functions)}]={{}};']
    lines += aot._render_function_declarations(functions)
    original_functions = {f['entry']:f for f in program['functions']}
    for index, f in enumerate(functions):
        triangle = args.optimized and f.entry == 0x2BFCB4
        tails = sorted({c['target'] for c in original_functions[f.entry]['tail_calls']})
        suffix = '_body' if args.coverage or triangle or tails else ''
        lines += aot._render_function(f,symbols,frozenset(),
            callback_free=args.optimized,symbol_suffix=suffix)
        if suffix:
            lines += [f'Oot3dWholeAotFlow Execute_{symbols[f.entry]}(',
                'Oot3dWholeAotFrame& f,Oot3dWholeAotContext& c,Oot3dAotArchitecturalState& s,uint32_t pc) {']
            if args.coverage:
                lines += [f'++familyVisits[{index}];']
            if triangle:
                lines += ['return FamilyTriangle(f,c,s,pc);']
            else:
                lines += [f'auto flow=Execute_{symbols[f.entry]}_body(f,c,s,pc);']
                if tails:
                    lines += ['if(flow.Kind==Oot3dWholeAotFlowKind::Branch) { switch(flow.Pc) {']
                    for tail in tails:
                        lines += [f'case 0x{tail:08X}U: return Execute_{symbols[tail]}(f,c,s,flow.Pc);']
                    lines += ['default: break;','}}']
                lines += ['return flow;']
            lines += ['}']
    by_entry = {f.entry:f for f in functions}
    lines += ['bool FamilyObserved(uint32_t root,const uint32_t* pcs,size_t count) {',
              'switch(root) {']
    for root in roots:
        lines += [f'case 0x{root:08X}U: {{']
        for entry in closure(program,root,args.max_functions):
            f = by_entry[entry]
            lo=min(b.pc for b in f.blocks)
            hi=max(b.instructions[-1].pc+4 for b in f.blocks)
            lines += [f'{{ auto p=std::lower_bound(pcs,pcs+count,0x{lo:08X}U);',
                      f'if(p!=pcs+count && *p<0x{hi:08X}U) return true; }}']
        lines += ['return false; }']
    lines += ['default: return true;','}','}',
        'Oot3dWholeAotFlow RunFamily(Oot3dWholeAotFrame& f,Oot3dWholeAotContext& c,Oot3dAotArchitecturalState& s,uint32_t pc) {',
        'switch(pc) {']
    for root in roots:
        lines += [f'case 0x{root:08X}U: return Execute_{symbols[root]}(f,c,s,pc);']
    lines += ['default: return Oot3dAotBranch(pc);','}','}']
    args.output.mkdir(parents=True,exist_ok=False)
    args.output=args.output.resolve()
    generated=args.output/'family_generated.inc'
    generated.write_text('\n'.join(lines))
    runtime=REPO/'tools/oot3d/native_game_runtime'
    a32=REPO/'tools/oot3d/native_a32_runtime'
    source=Path(__file__).with_name('aot_family_probe.cpp')
    obj=args.output/'family.obj'; dll=args.output/'family.dll'
    commands=[[str(args.compiler),'/nologo','/c','/MT','/O2','/EHsc','/std:c++20',
        '/DNOMINMAX','/D_CRT_SECURE_NO_WARNINGS','/fp:strict','/clang:-ffp-contract=off',
        *(f'/I{x}' for x in (args.output,runtime,a32,a32/'upstream',args.include)),
        str(source),f'/Fo{obj}'],
        [str(args.compiler),'/nologo','/LD','/MT',str(obj),str(args.support),f'/Fe{dll}',
         '-fuse-ld=lld','/link','/NOIMPLIB','/threads:2']]
    start=time.monotonic()
    for command in commands:
        subprocess.run(command,check=True,timeout=120)
    report={'developer_only':True,'roots':[f'{e:08X}' for e in roots],
        'functions':[f'{e:08X}' for e in entries],'optimized':args.optimized,'coverage':args.coverage,
        'build_seconds':time.monotonic()-start,'commands':commands,
        'hashes':{str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in
            (args.program,args.code,args.support,source,generated,Path(aot.__file__),
             a32/'oot3d_cpu_geometry_kernel_probe.cpp',dll)}}
    if args.selection:
        report['hashes'][str(args.selection)] = hashlib.sha256(args.selection.read_bytes()).hexdigest()
    (args.output/'build.json').write_text(json.dumps(report,indent=2))
    print(json.dumps({'functions':len(entries),'build_seconds':report['build_seconds'],'dll':str(dll)}))


if __name__=='__main__':
    main()
