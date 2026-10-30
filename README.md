# CardLens OCR

轻量、离线、跨平台的证件识别 SDK。C++ 内核基于 NCNN 与 PP-OCRv5，提供文字识别、字段提取、位置和置信分数。

[在线体验](https://mr-addict.github.io/cardlens-ocr/) · [MIT](LICENSE)

| 证件 | documentType | side |
| --- | --- | --- |
| 身份证人像面 / 国徽面 | `id_card` | `portrait` / `emblem` |
| 驾驶证主页 | `driver_license` | `main` |
| 行驶证主页 | `vehicle_license` | `main` |

固定的 4 张公开参考图、27 个标注字段全部匹配。该结果用于功能回归；实际效果受清晰度、反光和遮挡影响。`complete` 表示字段齐全且校验通过，`partial` 表示需要复核，具体提示见 `warnings`。

## Web

```js
import ocr from './sdk/browser.js';

await ocr.initialize();
const result = await ocr.recognize(file, {
  documentType: 'id_card', side: 'portrait'
});
console.log(result.fields);
await ocr.release();
```

输入为 JPEG/PNG 的 `Blob`、`ArrayBuffer` 或 `Uint8Array`。首次加载约 21 MB 模型，识别在浏览器本地完成。静态部署时保持 JS、Worker、WASM 和模型的相对目录；通过 `initialize({ modelBaseUrl })` 可指定模型地址。

## Node.js

```js
import ocr from './out/packages/cardlens-ocr/node.js';

await ocr.initialize();
console.log(await ocr.recognize('id-card.jpg', {
  documentType: 'id_card', side: 'portrait'
}));
await ocr.release();
```

支持文件路径、文件 URL 和图片字节，通过 Sharp 解码，在 Worker 中执行相同 WASM 内核。`initialize({ modelDirectory })` 可指定模型目录。

## Python

```sh
python -m pip install ./sdk/python
```

```python
from cardlens_ocr import ocr

ocr.initialize('models', library='out/build/native/libcardlens.so')
try:
    result = ocr.recognize('id-card.jpg', {
        'documentType': 'id_card', 'side': 'portrait'
    })
    print(result['fields'])
finally:
    ocr.release()
```

Windows 使用 `cardlens.dll`，macOS 使用 `libcardlens.dylib`。Python 接收图片路径或字节，通过 Pillow 解码，调用原生 C 接口。

## C/C++

```cpp
#include <cardlens_ocr/core.h>

cardlens_ocr::Ocr ocr;
ocr.initialize("models");
auto json = ocr.recognize(rgba, width, height, stride, "id_card", "portrait");
ocr.release();
```

输入为 RGBA 像素，`stride` 的单位为字节，返回 UTF-8 JSON。C 接口见 [`c_api.h`](core/include/cardlens_ocr/c_api.h)，完整示例见 [`examples`](examples)。同一对象按顺序初始化、识别和释放。

## 鸿蒙

```typescript
import ocr from 'cardlens-ocr';

await ocr.initialize(context);
const result = await ocr.recognize(imageBytes, {
  documentType: 'id_card', side: 'portrait'
});
ocr.release();
```

模型随 HAR 内置。输入为 JPEG/PNG 的 `ArrayBuffer`，通过 Image Kit 解码并调用共享内核。支持 ARM64；真机运行与性能待验证。

## 开发

需要 Python 3.12+、CMake 3.20+ 和 C++17 编译器。原生构建配置覆盖 Linux、macOS、Windows。

Linux 本机构建通过 systemd 用户服务隔离，默认单任务、单核 25% CPU 和 2 GiB 内存上限。Emscripten、Binaryen 与数值库使用单线程。

```sh
python scripts/build.py native
python tests/fetch_references.py
ctest --test-dir out/build/native -C Release --output-on-failure
```

Web / Node.js 使用 Node.js 24+、pnpm 12 和 Emscripten 6.0.13：

```sh
pnpm install --frozen-lockfile
python scripts/build.py wasm
pnpm build
pnpm test:node
pnpm exec playwright install chromium
pnpm test:web
python -m http.server 4173 --directory out/site
```

鸿蒙构建需配置官方命令行工具及 `OHOS_NDK_HOME`，运行 `bash scripts/build-harmonyos.sh`。

`core/` 保存识别内核，`sdk/` 保存各端封装，`apps/demo/` 保存在线体验页面。模型来源与校验值见 [`models/manifest.json`](models/manifest.json)，依赖许可见 [`NOTICE`](NOTICE)。

当前已通过 Linux 原生、Python 和鸿蒙 HAR 检查；Web / Node.js 的 WASM 构建与识别验证交由 GitHub Actions 执行。
