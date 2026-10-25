# Qt Lottie GPU demo

Approved by the user on 2026-10-24 (“实现”) after the design in chat.

## Scope

A Qt 6 Widgets, C++17 desktop demo for macOS and Windows. ThorVG is built
as a static library with its OpenGL GPU backend. QOpenGLWidget supplies the
current context and framebuffer. Request OpenGL 4.1 Core on macOS and 3.3
Core elsewhere, with depth/stencil buffers. No CPU rendering fallback.

Open Lottie JSON, play/pause, loop, scrub and fit to the viewport. List text
layers in the root and asset precompositions and image assets. Edit all text
document keyframes in a selected layer, preserving styling and animation.
Replace an image with a PNG normalized to the asset's original dimensions.
Reload the animation while preserving normalized playback position and state.
Save JSON with embedded images, including originally external local images.

Live text is editable; text converted to shape outlines is not. Shared
precomposition/image assets affect every instance. No dotLottie ZIP support,
remote resource fetching, timeline authoring, or animation/video export.
Font selection loads a TTF/OTF file for preview; fonts are not embedded in JSON.
Missing fonts use an explicitly reported system fallback where available.

## Components

- LottieDocument: JSON validation, layer inventory, edits, portable image
  resources, save through QSaveFile. Failed opens/edits leave prior state intact.
- LottieWidget: owns animation and GL canvas, sets target every paint using
  physical pixel dimensions and current Qt FBO. Sync before mutation/destruction.
  Release canvas resources with a current context, including context recreation.
- MainWindow: playback and editing controls, file operations, font selection,
  actionable errors, unsaved-change guard, bundled sample on startup.
- Tests: document transformations and a real Qt/ThorVG GPU smoke test covering
  playback, resize, replacement, reload, and nonempty framebuffer output.

## Delivery and validation

Keep changes in examples/qt-lottie-editor plus docs, without changing ThorVG.
CMake builds the demo and invokes ThorVG's Meson build via ExternalProject.
Document macOS and Windows prerequisites, build/run/deployment commands and
limitations. Verify on the available macOS/Qt installation; do not claim Windows
runtime verification without a Windows host.

The parent Git repository has no commits and this source tree is untracked;
there is no usable base for worktrees, commits or a branch diff. Work directly
in the new example directory without changing the parent repository state.
