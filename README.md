# CardLens OCR

轻量、离线、跨平台的证件识别 SDK。C++ 内核基于 NCNN 与 PP-OCRv5，提供文字识别、字段提取、位置和置信分数。

[在线体验](https://mr-addict.github.io/cardlens-ocr/) · [MIT](LICENSE)

| 证件 | documentType | side |
| --- | --- | --- |
| 身份证人像面 / 国徽面 | `id_card` | `portrait` / `emblem` |
| 驾驶证主页 | `driver_license` | `main` |
| 行驶证主页 | `vehicle_license` | `main` |

固定的 4 张公开参考图、27 个标注字段全部匹配。该结果用于功能回归；实际效果受清晰度、反光和遮挡影响。`complete` 表示必需字段齐全、校验通过且没有待复核问题，`partial` 表示需要复核，`failed` 表示未获得有效结果或证件不符。`warnings` 保留提示原因；`CAPTION_REMOVED` 为清理提示，不影响完成状态。模型分数未经正确率校准。

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

Web 接入需要 HTTPS 或 localhost。输入为 JPEG/PNG 的 `Blob`、`ArrayBuffer` 或 `Uint8Array`。首次加载约 21 MB 模型，解码与识别在 Worker 中本地完成，模型加载时校验 SHA-256。静态部署时保持 JS、Worker、WASM 和模型的相对目录；通过 `initialize({ modelBaseUrl })` 可指定模型地址。

演示页支持字段定位与局部放大、复制和 JSON 导出。在 HTTPS 或 localhost 下，资源缓存完成后可断网刷新并识别；页面会显示缓存状态。自定义模型地址的离线缓存由接入方负责。

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

异步 SDK 在操作未完成时拒绝并发识别与释放，返回 `BUSY`；Python 并发操作也返回 `BUSY`。重复初始化返回 `ALREADY_INITIALIZED`，释放后可重新初始化。JavaScript Worker 异常或五分钟无响应会释放 Worker，调用方可重试初始化。

结果中的坐标均对应按 EXIF 方向解码后的输入图，单位为像素；`rotation` 表示识别采用的顺时针旋转角度。

## 开发

需要 Python 3.12+、CMake 3.20+ 和 C++17 编译器。原生构建配置覆盖 Linux、macOS、Windows。

Linux 本机构建通过 systemd 用户服务隔离，默认单任务、单核 25% CPU 和 2 GiB 内存上限。Emscripten、Binaryen 与数值库使用单线程。

```sh
python scripts/build.py native
python tests/fetch_references.py
ctest --test-dir out/build/native -C Release --output-on-failure
```

已有依赖缓存时，可在原生构建命令中添加 `--use-installed-deps`。

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

Linux 本机浏览器检查使用 `python scripts/build_limits.py --tasks 128 pnpm test:web`。

鸿蒙构建需配置官方命令行工具及 `OHOS_NDK_HOME`，运行 `bash scripts/build-harmonyos.sh`。

`core/` 保存识别内核，`sdk/` 保存各端封装，`apps/demo/` 保存在线体验页面。模型来源与校验值见 [`models/manifest.json`](models/manifest.json)，依赖许可见 [`NOTICE`](NOTICE)。

已在 Linux 验证 WASM 构建、Web / Node.js 识别、Python 安装调用、原生回归和鸿蒙 HAR 构建。

`tests/evaluate.py` 按样本 `split` 分别统计字段匹配、错误结果被判完整的比例、初始化与识别耗时、原生进程峰值内存；`--compare` 对照完整方向搜索。`tests/prepare_variants.py` 生成三类证件的旋转、缩放、透视、背景、模糊、反光和遮挡回归样本。独立评估可通过 `--manifest`、`--images` 指定人工标注数据，条目沿用 `tests/gold.json` 格式并添加 `split`；变换参考图仍属于回归数据。
