@echo off
rem Build the patched clang + lld for the Xbox 360 target.
rem Only the PowerPC backend is built, which keeps the build to a fraction of a
rem full LLVM.
rem The checkout must include: llvm clang lld cmake third-party libc libunwind
rem   git sparse-checkout set llvm clang lld cmake third-party libc libunwind
rem libc is required by llvm/CMakeLists.txt even when the project is disabled,
rem and libunwind supplies the mach-o headers lld's MachO backend includes.
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

set "SRC=D:\Git\RXDK-360\vendor\llvm-project\llvm"
set "BLD=D:\Git\RXDK-360\build\llvm"
set "NINJA=C:\Program Files\Microsoft Visual Studio\18\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"

if not exist "%BLD%\build.ninja" (
  cmake -G Ninja -S "%SRC%" -B "%BLD%" ^
    -DCMAKE_MAKE_PROGRAM="%NINJA%" ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DLLVM_ENABLE_PROJECTS=clang;lld ^
    -DLLVM_TARGETS_TO_BUILD=PowerPC ^
    -DLLVM_ENABLE_ASSERTIONS=ON ^
    -DLLVM_OPTIMIZED_TABLEGEN=ON ^
    -DLLVM_INCLUDE_TESTS=OFF ^
    -DLLVM_INCLUDE_BENCHMARKS=OFF ^
    -DLLVM_INCLUDE_EXAMPLES=OFF ^
    -DCLANG_INCLUDE_TESTS=OFF ^
    -DLLVM_ENABLE_ZLIB=OFF ^
    -DLLVM_ENABLE_ZSTD=OFF ^
    -DLLVM_ENABLE_LIBXML2=OFF || exit /b 1
)

cmake --build "%BLD%" --target clang lld llc || exit /b 1
echo BUILD_OK
