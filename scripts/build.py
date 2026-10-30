#!/usr/bin/env python3
"""Build CardLens OCR and its pinned dependencies."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'out'

def run(args):
    print('+', ' '.join(map(str, args)), flush=True)
    subprocess.run(list(map(str, args)), check=True)

def sources():
    downloads = OUT / 'downloads'
    downloads.mkdir(parents=True, exist_ok=True)
    base = OUT / 'sources'
    base.mkdir(parents=True, exist_ok=True)
    wanted = {
        'ncnn.tar.gz': 'ncnn',
        'opencv.tar.gz': 'opencv-31b0eeea0b44b370fd0712312df4214d4ae1b158',
        'clipper2.tar.gz': 'Clipper2-Clipper2_1.5.4',
    }
    for row in json.loads((ROOT / 'config/downloads.json').read_text(encoding='utf-8')):
        if row['file'] not in {*wanted, 'json.hpp'}:
            continue
        path = downloads / row['file']
        if not path.exists():
            partial = path.with_suffix('.partial')
            urllib.request.urlretrieve(row['url'], partial)
            partial.replace(path)
        if hashlib.sha256(path.read_bytes()).hexdigest() != row['sha256']:
            raise RuntimeError('Checksum mismatch: ' + row['file'])
        if row['file'] in wanted and not (base / wanted[row['file']]).exists():
            with tarfile.open(path) as archive:
                top = archive.getnames()[0].split('/')[0]
                archive.extractall(base, filter='data')
            if row['file'] == 'ncnn.tar.gz':
                (base / top).rename(base / 'ncnn')
    common = OUT / 'deps/common'
    (common / 'include/nlohmann').mkdir(parents=True, exist_ok=True)
    shutil.copy2(downloads / 'json.hpp', common / 'include/nlohmann/json.hpp')
    if not (common / 'clipper2').exists():
        shutil.copytree(base / wanted['clipper2.tar.gz'] / 'CPP/Clipper2Lib', common / 'clipper2')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('target', choices=['native', 'wasm', 'harmonyos'], nargs='?', default='native')
    parser.add_argument('--jobs', type=int, default=4)
    parser.add_argument('--deps-only', action='store_true')
    args = parser.parse_args()
    manifest = json.loads((ROOT / 'models/manifest.json').read_text(encoding='utf-8'))
    for entry in manifest['files']:
        path = ROOT / 'models' / entry['file']
        if hashlib.sha256(path.read_bytes()).hexdigest() != entry['sha256']:
            raise RuntimeError('Model checksum mismatch: ' + entry['file'])
    sources()
    kind = {'native': 'host', 'wasm': 'wasm', 'harmonyos': 'arm64'}[args.target]
    prefix = OUT / 'deps' / kind
    cmake = ['cmake']
    flags = ['-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_POSITION_INDEPENDENT_CODE=ON',
             '-DCMAKE_INSTALL_LIBDIR=lib', '-DBUILD_SHARED_LIBS=OFF']
    if args.target == 'wasm':
        emsdk = Path(os.environ.get('EMSDK', OUT / 'tools/emsdk'))
        wrapper = emsdk / 'upstream/emscripten/emcmake.py'
        if not wrapper.exists():
            raise RuntimeError('Install and activate Emscripten 6.0.13, or set EMSDK.')
        cmake = [sys.executable, wrapper, 'cmake']
        flags += ['-DCMAKE_C_FLAGS=-fexceptions', '-DCMAKE_CXX_FLAGS=-fexceptions']
    if args.target == 'harmonyos':
        ndk = os.environ.get('OHOS_NDK_HOME')
        if not ndk:
            raise RuntimeError('Set OHOS_NDK_HOME to the HarmonyOS native SDK.')
        flags += [f'-DCMAKE_TOOLCHAIN_FILE={ndk}/build/cmake/ohos.toolchain.cmake',
                  '-DOHOS_ARCH=arm64-v8a', '-DOHOS_STL=c++_shared']
    def configure(source, build, extra):
        run(cmake + ['-S', source, '-B', build, *flags, *extra])
        run(['cmake', '--build', build, '--config', 'Release', '--parallel', args.jobs])
    for name, location in [('ncnn', 'ncnn'), ('opencv', 'opencv-31b0eeea0b44b370fd0712312df4214d4ae1b158')]:
        build = OUT / 'build' / f'{name}-{kind}'
        # Successful installations are reusable; source and options are pinned by target.
        stamp = prefix / f'.cardlens-{name}'
        if stamp.exists():
            continue
        if name == 'ncnn':
            extra = ['-DNCNN_VULKAN=OFF', '-DNCNN_OPENMP=OFF', '-DNCNN_BUILD_TOOLS=OFF',
                     '-DNCNN_BUILD_EXAMPLES=OFF', '-DNCNN_BUILD_BENCHMARK=OFF',
                     '-DNCNN_BUILD_TESTS=OFF', '-DNCNN_SHARED_LIB=OFF', '-DNCNN_AVX512=OFF',
                     '-DNCNN_THREADS=' + ('OFF' if kind == 'wasm' else 'ON')]
        else:
            modules = 'core,imgproc,imgcodecs' if kind == 'host' else 'core,imgproc'
            extra = [f'-DBUILD_LIST={modules}', '-DBUILD_TESTS=OFF', '-DBUILD_PERF_TESTS=OFF',
                     '-DBUILD_EXAMPLES=OFF', '-DBUILD_opencv_apps=OFF', '-DBUILD_JAVA=OFF',
                     '-DBUILD_opencv_python3=OFF', '-DBUILD_PROTOBUF=OFF', '-DCPU_DISPATCH=',
                     '-DWITH_IPP=OFF', '-DWITH_OPENCL=OFF', '-DWITH_ITT=OFF', '-DWITH_FFMPEG=OFF',
                     '-DWITH_GSTREAMER=OFF', '-DWITH_QT=OFF', '-DWITH_GTK=OFF', '-DWITH_PROTOBUF=OFF',
                     '-DWITH_TIFF=OFF', '-DWITH_WEBP=OFF', '-DWITH_OPENEXR=OFF', '-DWITH_OPENJPEG=OFF',
                     '-DWITH_QUIRC=OFF', '-DWITH_ADE=OFF', '-DWITH_JASPER=OFF']
            if kind != 'host':
                extra += ['-DWITH_PNG=OFF', '-DWITH_JPEG=OFF']
            if kind == 'wasm':
                extra += ['-DCV_ENABLE_INTRINSICS=OFF', '-DWITH_PTHREADS_PF=OFF',
                          '-DCPU_BASELINE=', '-DBUILD_opencv_js=OFF']
            if platform.system() == 'Windows':
                extra += ['-DBUILD_WITH_STATIC_CRT=OFF']
        configure(OUT / 'sources' / location, build, [f'-DCMAKE_INSTALL_PREFIX={prefix}', *extra])
        run(['cmake', '--install', build, '--config', 'Release'])
        stamp.touch()
    if args.deps_only:
        return
    extra = [f'-DCARDLENS_DEPS={prefix}', f'-DCARDLENS_COMMON={OUT}/deps/common']
    if platform.system() == 'Windows':
        configs = list(prefix.rglob('OpenCVConfig.cmake'))
        if configs:
            extra.append(f'-DOpenCV_DIR={configs[0].parent}')
    configure(ROOT, OUT / 'build' / args.target, extra)

if __name__ == '__main__':
    main()
