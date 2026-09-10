import os, subprocess, sys
SDK=r"C:\Program Files (x86)\Microsoft Xbox 360 SDK"; BIN=os.path.join(SDK,"bin","win32")
env=dict(os.environ); env["PATH"]=BIN+os.pathsep+env["PATH"]
env["INCLUDE"]=os.path.join(SDK,"include","xbox"); env["LIB"]=os.path.join(SDK,"lib","xbox")
D=os.path.dirname(os.path.abspath(__file__))
def run(c):
    r=subprocess.run(c,env=env,cwd=D,capture_output=True,text=True)
    if r.returncode!=0: print("FAIL",c[0]); print(r.stdout[-800:]); print(r.stderr[-300:]); sys.exit(1)
run([os.path.join(BIN,"cl.exe"),"/nologo","/c","/MT","/EHsc","/D_XBOX","/Fotc2.obj","tc2.cpp"])
run([os.path.join(BIN,"cl.exe"),"/nologo","/c","/MT","/EHsc","/D_XBOX","/Fomain2.obj","main2.cpp"])
run([os.path.join(BIN,"link.exe"),"/nologo","/OUT:tc2.exe","tc2.obj","main2.obj","xapilib.lib","xboxkrnl.lib","libcMT.lib"])
run([os.path.join(BIN,"imagexex.exe"),"/nologo","/IN:tc2.exe","/OUT:tc2.xex"])
print("built tc2.xex", os.path.getsize(os.path.join(D,"tc2.xex")))
