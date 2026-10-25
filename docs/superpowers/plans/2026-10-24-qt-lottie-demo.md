# Qt Lottie GPU Demo Implementation Plan

> **For agentic workers:** Execute inline with executing-plans; use
> requesting-code-review for an independent review before delivery.

**Goal:** Deliver a runnable Qt desktop Lottie GPU player with text/image editing.

**Architecture:** Separate JSON editing from the Qt OpenGL renderer and window.
Build ThorVG using its existing Meson configuration, exposed as a CMake target.

**Tech Stack:** Qt 6 Widgets/OpenGLWidgets/Test, C++17, CMake, Meson, Ninja.

## Global Constraints

- Support macOS and Windows; validate locally on macOS.
- Use the ThorVG OpenGL GPU backend, without CPU fallback.
- Preserve playback position/state when applying edits.
- Save images inside JSON; preserve text formatting and keyframe times.
- Do not modify unrelated ThorVG sources or the parent Git repository.

## Task 1: JSON document and behavioral tests

Create examples/qt-lottie-editor/{CMakeLists.txt,src/lottiedocument.h,
src/lottiedocument.cpp,tests/document_test.cpp,assets/demo.json}.

Interfaces: `bool load(const QByteArray&, const QString& baseDir, QString* error)`,
`QList<Entry> entries() const`, `QString text(int) const`,
`bool replaceText(int, const QString&, QString*)`,
`bool replaceImage(int, const QImage&, QString*)`, `QByteArray json() const`,
`bool save(const QString&, QString*) const`.

- [x] Add Qt Test cases for text in precompositions, all text keyframes,
  embedded image dimensions, external resource portability, invalid JSON and
  atomic failure. Configure tests-only CMake and observe missing implementation.
- [x] Implement recursive JSON paths and portable document edits. Use
  `QSaveFile::commit()` for saving; return meaningful errors without mutation.
- [x] Run `ctest --test-dir build/qt-lottie -R document --output-on-failure`.

## Task 2: GPU renderer and playback

Create src/lottiewidget.{h,cpp} and tests/gpu_test.cpp. Use
`canvas->target(nullptr, nullptr, nullptr, defaultFramebufferObject(), w, h,
tvg::ColorSpace::ABGR8888S)` while the Qt context is current.

- [x] Write a GPU test that loads bundled JSON, shows a real widget, advances
  frames, resizes, reloads after text/image edits, checks preserved position and
  state and compares captured framebuffer images.
- [x] Implement elapsed-time based animation, single GUI-thread rendering,
  context lifecycle cleanup, font resolver, error signals and checked TVG results.
- [x] Build and run the GPU test on the local window server.

## Task 3: Application and delivery

Create src/main.cpp, src/mainwindow.{h,cpp}, README.md and resources.qrc.

- [x] Implement toolbar, preview, text/image inventory, editor, timeline,
  font loading, transactional load/edit, unsaved-change prompts and errors.
- [x] Bundle a small original sample with live text, an embedded image and
  animated transforms. Open it automatically, with CLI path override.
- [x] Document exact CMake, prerequisites and deployment commands for both OSes.
- [x] Run document/GPU tests and the demo; independently review new files and
  correct important findings. Record actual evidence and platform limitations.

## Final verification — 2026-10-24

- `cmake --build build/qt-lottie -j 6`: exit 0; app, static dependency and all test executables built.
- `ctest --test-dir build/qt-lottie --output-on-failure`: 3/3 passed (document, gpu, ui).
- Environment: macOS 26.5.1 arm64, Qt 6.12.0, Apple M5 Pro, desktop OpenGL 4.1.
- Document Qt Test suite: 19 passed including setup/teardown.
- GPU Qt Test suite: context recreation plus playback/edit/resize/reload; 4 passed including setup/teardown.
- UI Qt Test suite: Chinese live text preview, pending-edit layer switching and transport; 3 passed including setup/teardown.
- `build/qt-lottie/demo-preview.png` inspected: sample, text controls, timeline and GPU information visible.
- Independent review: GPU resource lifetime corrected and re-reviewed; no remaining important findings.
- Fixed both null JSON field insertion and old-renderer reuse after context destruction, with failing regressions observed before fixes.
- Demo launched successfully and left running for the user. Windows instructions and WGL integration supplied, Windows runtime not tested.
- Parent Git repository is unborn; no branch/commit/merge/push operation performed.
