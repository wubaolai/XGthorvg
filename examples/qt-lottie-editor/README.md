# Qt 6 Lottie GPU Editor

使用 **Qt 6 Widgets、C++17、ThorVG 1.1.3 OpenGL 后端**的桌面示例。
源码位于当前 ThorVG 仓库内；Demo 的 CMake 会调用 ThorVG 自己的 Meson，
构建并静态链接 ThorVG，无需手动把渲染器源码加入 Qt 工程。

## 功能

- 打开 `.json` / `.lot`，播放、暂停、循环、拖动进度，按窗口等比适配，支持 HiDPI。
- 启动时打开原创内置动画，包含两个可编辑文字图层和一张动态图片。
- 枚举主合成和预合成中的文字、图片资源；输入文字后 350 ms 自动应用。
- 替换图片：转换成 PNG，等比缩放到原资源尺寸并居中留透明边，保留图层动画。
- 修改后重新加载，保持播放位置与播放/暂停状态。
- 另存为 JSON，嵌入图片；打开时也会将原有外部本地图片转为内嵌资源。
- 加载 TTF/OTF，作为缺失字体的预览回退；底部显示 GPU 和字体信息。
- JSON 编辑单元测试和真实 OpenGL 帧缓冲集成测试。

## 构建依赖

- Qt **6.5 或以上**，包括 Core、Gui、Widgets、OpenGL、OpenGLWidgets；测试需要 Test。
- CMake 3.22 或以上、C++17 编译器、Python、Meson、Ninja。
- **桌面 OpenGL 3.3 或以上**和对应显卡驱动。macOS 请求 OpenGL 4.1 Core。
- 推荐 Release 构建。默认不启用 OpenMP；启用 Lottie expressions。

以下命令均从 **ThorVG 仓库根目录**执行。

### macOS

安装 Xcode Command Line Tools 和 Qt macOS 套件。Qt 路径按本机版本调整。

```sh
python3 -m venv .venv-demo
.venv-demo/bin/python -m pip install meson ninja
export PATH="$PWD/.venv-demo/bin:$PATH"

cmake -S examples/qt-lottie-editor -B build/qt-lottie -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$HOME/Qt/6.12.0/macos"
cmake --build build/qt-lottie

open build/qt-lottie/qt_lottie_editor.app
# 或直接运行并指定动画，路径支持空格和中文：
build/qt-lottie/qt_lottie_editor.app/Contents/MacOS/qt_lottie_editor /path/to/animation.json
```

发布到其他 Mac 前，用当前 Qt 套件的 `macdeployqt` 将 Qt 依赖拷贝进 App：

```sh
"$HOME/Qt/6.12.0/macos/bin/macdeployqt" build/qt-lottie/qt_lottie_editor.app
```

ThorVG 和 Qt 必须使用相同架构；示例构建本机架构，不会自动生成 universal binary。

### Windows / MSVC

安装 Visual Studio 2022 的 C++ 桌面开发工具和匹配的 Qt MSVC 64 位套件。
在 **x64 Native Tools Command Prompt for VS 2022** 中执行，保证 Meson 与 CMake
均使用同一套 MSVC 编译器。下面是 `cmd.exe` 命令，路径按本机调整：

```bat
py -m venv .venv-demo
.venv-demo\Scripts\python -m pip install meson ninja
set "PATH=%CD%\.venv-demo\Scripts;%PATH%"

cmake -S examples/qt-lottie-editor -B build\qt-lottie -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_PREFIX_PATH=C:\Qt\6.12.0\msvc2022_64
cmake --build build\qt-lottie

C:\Qt\6.12.0\msvc2022_64\bin\windeployqt.exe --release build\qt-lottie\qt_lottie_editor.exe
build\qt-lottie\qt_lottie_editor.exe
```

Qt、Demo、ThorVG 均需匹配 x64 和 MSVC 运行库；本示例使用 Release `/MD`。
Windows 需要厂商桌面 OpenGL 驱动，不能使用 ANGLE/OpenGL ES。Windows 构建命令
已提供，但本次开发环境是 macOS，未在 Windows 上实际运行。

## 使用

1. 启动后可直接播放内置示例，也可点击“打开动画”。
2. 在右侧选择“文字”条目后输入内容；自动预览，或点击“应用并预览”。
3. 选择“图片”条目，点击“更换图片”。图片资源可能被多个图层共享。
4. 缺字或字体不符时点击“预览字体”，选择覆盖所需字符的 TTF/OTF。
5. 点击“另存为”，输出包含内嵌图片的 Lottie JSON。

## 测试

```sh
ctest --test-dir build/qt-lottie --output-on-failure
```

`gpu` 和 `ui` 测试会创建实际窗口，验证播放/暂停、循环、拖动、缩放、更换文字和图片后的
真实帧缓冲变化、重复重载和清理。需要登录图形桌面和可用 GPU，不使用 offscreen
平台插件或 CPU fallback。仅运行 JSON 测试时不需要窗口服务器：

```sh
cmake -S examples/qt-lottie-editor -B build/qt-lottie-document \
  -DCMAKE_PREFIX_PATH="$HOME/Qt/6.12.0/macos" -DLOTTIE_DOCUMENT_ONLY=ON
cmake --build build/qt-lottie-document
ctest --test-dir build/qt-lottie-document --output-on-failure
```

可选：`LOTTIE_TEST_CAPTURE=/tmp/lottie-gpu.png build/qt-lottie/gpu_test` 保存测试画面；
`LOTTIE_UI_CAPTURE=/tmp/lottie-ui.png build/qt-lottie/ui_test` 保存完整窗口截图。

## 实现要点与边界

- `LottieDocument` 只负责 JSON 与资源；`LottieWidget` 管理 ThorVG、GPU 资源和时钟；
  `MainWindow` 负责界面。没有修改 ThorVG 的核心代码。
- 在 `paintGL()` 内使用 Qt 当前的 `defaultFramebufferObject()` 和物理像素尺寸。
  不把 FBO 0 当作 Qt 渲染目标。GL 资源在上下文为 current 时创建/销毁。
- 文字替换修改所选图层全部文字关键帧的字符串，保留字号、颜色、时间等属性。
  文字已经导出为矢量轮廓时，不能恢复成可编辑文字；内嵌 glyph 只覆盖原有字符时，
  新增字符可能需要重新导出成字体文字。
- 预合成、图片资源可能被共享；编辑资源会改变其全部实例。已有 slot override
  或表达式驱动的内容可能覆盖源属性；本示例不提供完整 AE/时间轴编辑功能。
  根级 `slots` 内的外部图片覆盖资源不会自动嵌入，保存这类文件时需保留其资源路径。
- 保存时不嵌入字体。预览回退字体只影响当前程序；其他播放器需提供对应字体。
  ThorVG 的字体排版能力决定最终效果，示例不增加复杂文字排版支持。
- 支持本地图片及 base64 内嵌图片；不下载网络资源、不解包 `.lottie` ZIP、
  不支持视频/音频资源或导出视频。无法读取的外部图片会明确报错并保留旧文档。
- OpenGL 初始化或渲染失败会显示错误，不自动切换为 CPU 渲染。

Qt FBO 与上下文生命周期遵循 [QOpenGLWidget 官方说明](https://doc.qt.io/qt-6/qopenglwidget.html)。
ThorVG 及本示例随仓库的 MIT 许可证分发；Qt 及所选字体遵循各自许可证。
