"""Build native A/B/C versions using the task-local Microsoft toolchain."""
from pathlib import Path
import subprocess,os,json,hashlib,sys
root=Path(__file__).resolve().parents[1]
import argparse,shutil
parser=argparse.ArgumentParser(description='Build TPFC Quiet and run its software checks. No hardware control is started.')
parser.add_argument('--toolchain',type=Path,help='Optional portable MSVC/Windows SDK root; otherwise use an x86 Native Tools developer prompt')
args=parser.parse_args()
sdkroot=args.toolchain or root/'tools/msvc'
env=os.environ.copy()
if sdkroot.is_dir():
 vc=next((sdkroot/'VC/Tools/MSVC').iterdir())
 sdk=next((sdkroot/'Windows Kits/10/Include').iterdir()).name
 kit=sdkroot/'Windows Kits/10'
 env['PATH']=str(vc/'bin/Hostx64/x86')+';'+str(kit/'bin'/sdk/'x64')+';'+env['PATH']
 env['INCLUDE']=';'.join(map(str,[vc/'include']+[kit/'Include'/sdk/p for p in ['ucrt','shared','um','winrt','cppwinrt']]))
 env['LIB']=';'.join(map(str,[vc/'lib/x86',kit/'Lib'/sdk/'ucrt/x86',kit/'Lib'/sdk/'um/x86']))
 cl=vc/'bin/Hostx64/x86/cl.exe';rc=kit/'bin'/sdk/'x64/rc.exe'
else:
 cl=shutil.which('cl.exe');rc=shutil.which('rc.exe')
 if not cl or not rc or not env.get('INCLUDE') or not env.get('LIB'):
  raise SystemExit('Open an x86 Native Tools Command Prompt for Visual Studio with the Windows SDK installed, then run: python tools/build.py')
out=root/'releases'/'3.5.5';out.mkdir(parents=True,exist_ok=True)
build=root/'build';build.mkdir(exist_ok=True)
def run(args,where):
 print('RUN',args[0],flush=True)
 r=subprocess.run(args,cwd=where,env=env,capture_output=True)
 log=r.stdout+r.stderr
 print(log.decode('utf-8',errors='replace'),flush=True)
 if r.returncode:raise SystemExit(r.returncode)
 return log
monitor_log=run([sys.executable,str(root/'tests/monitor_tests.py')],root)
(build/'monitor-tests.txt').write_bytes(monitor_log)
run([str(rc),'/nologo','/fo',str(build/'app.res'),'app.rc'],root/'quiet')
common=[str(cl),'/nologo','/std:c++17','/EHsc','/W4','/utf-8','/MT','/O2','/DUNICODE','/D_UNICODE','/DWIN32','/D_CRT_SECURE_NO_WARNINGS']
run(common+[str(root/'tests/policy_tests.cpp'),'/Fe:'+str(build/'policy_tests.exe'),'/link','/SUBSYSTEM:CONSOLE'],build)
test=subprocess.run([str(build/'policy_tests.exe')],capture_output=True)
(build/'policy-tests.txt').write_bytes(test.stdout+test.stderr)
print(test.stdout.decode('utf-8',errors='replace'))
if test.returncode:raise SystemExit(test.returncode)
native_libs=['user32.lib','gdi32.lib','shell32.lib','advapi32.lib','ole32.lib','oleaut32.lib','uuid.lib','powrprof.lib','runtimeobject.lib','wbemuuid.lib','rpcrt4.lib','wintrust.lib']
run(common+[str(root/'tests/vendor_tests.cpp'),'/Fe:'+str(build/'vendor_tests.exe'),'/link','/SUBSYSTEM:CONSOLE']+native_libs,build)
vendor_test=subprocess.run([str(build/'vendor_tests.exe')],capture_output=True,timeout=30)
(build/'vendor-tests.txt').write_bytes(vendor_test.stdout+vendor_test.stderr)
print(vendor_test.stdout.decode('utf-8',errors='replace'))
if vendor_test.returncode:raise SystemExit(vendor_test.returncode)
run(common+[str(root/'tests/power_tests.cpp'),'/Fe:'+str(build/'power_tests.exe'),'/link','/SUBSYSTEM:CONSOLE']+native_libs,build)
power_test=subprocess.run([str(build/'power_tests.exe')],capture_output=True,timeout=30)
(build/'power-tests.txt').write_bytes(power_test.stdout+power_test.stderr)
print(power_test.stdout.decode('utf-8',errors='replace'))
if power_test.returncode:raise SystemExit(power_test.returncode)
run(common+[str(root/'tests/recovery_tests.cpp'),'/Fe:'+str(build/'recovery_tests.exe'),'/link','/SUBSYSTEM:CONSOLE'],build)
recovery_test=subprocess.run([str(build/'recovery_tests.exe')],capture_output=True,timeout=30)
(build/'recovery-tests.txt').write_bytes(recovery_test.stdout+recovery_test.stderr)
print(recovery_test.stdout.decode('utf-8',errors='replace'))
if recovery_test.returncode:raise SystemExit(recovery_test.returncode)
run(common+[str(root/'tests/io_regression_355.cpp'),'/Fe:'+str(build/'io_regression_355.exe'),'/link','/SUBSYSTEM:CONSOLE']+native_libs,build)
io_test=subprocess.run([str(build/'io_regression_355.exe')],capture_output=True,timeout=30)
(build/'io-regression-tests.txt').write_bytes(io_test.stdout+io_test.stderr)
print(io_test.stdout.decode('utf-8',errors='replace'))
if io_test.returncode:raise SystemExit(io_test.returncode)
for test_stage in [1,2,3]:
 target=build/f'engine_tests_{test_stage}.exe'
 run(common+['/DQUIET_STAGE='+str(test_stage),str(root/'tests/engine_tests.cpp'),'/Fe:'+str(target),'/link','/SUBSYSTEM:CONSOLE','user32.lib','gdi32.lib','shell32.lib','advapi32.lib','ole32.lib','oleaut32.lib','uuid.lib','powrprof.lib','runtimeobject.lib','wbemuuid.lib','rpcrt4.lib','wintrust.lib'],build)
 integration=subprocess.run([str(target)],capture_output=True,timeout=30)
 (build/f'engine-tests-{test_stage}.txt').write_bytes(integration.stdout+integration.stderr)
 print(integration.stdout.decode('utf-8',errors='replace'))
 if integration.returncode:raise SystemExit(integration.returncode)
for stage,name in [(1,'A-信息可靠版'),(2,'B-Smart静音版'),(3,'C-日常完整版')]:
 d=out/name;d.mkdir(exist_ok=True)
 args=common+['/DQUIET_STAGE='+str(stage),str(root/'quiet/main.cpp'),str(build/'app.res'),'/Fe:'+str(d/'TPFCQuiet.exe'),'/link','/SUBSYSTEM:WINDOWS','/MANIFEST:NO','/DYNAMICBASE','/NXCOMPAT','user32.lib','gdi32.lib','shell32.lib','advapi32.lib','ole32.lib','oleaut32.lib','uuid.lib','comctl32.lib','gdiplus.lib','powrprof.lib','runtimeobject.lib','wbemuuid.lib','rpcrt4.lib','wintrust.lib']
 log=run(args,build);(build/f'build-{stage}.log').write_bytes(log)
 (d/'version.txt').write_text(f'TPFC Quiet 3.5.5-{chr(64+stage)}\nDefault: Quiet office\nMachine certification and live CPU/GPU protection required for Smart control\n',encoding='utf-8')
manifest={str(p.relative_to(out)):hashlib.sha256(p.read_bytes()).hexdigest() for p in out.rglob('*.exe')}
(out/'SHA256.json').write_text(json.dumps(manifest,indent=2,ensure_ascii=False),encoding='utf-8')
print('BUILT',json.dumps(manifest,ensure_ascii=False),flush=True)
