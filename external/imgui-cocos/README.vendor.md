# Vendored gd-imgui-cocos

This is a vendored copy of [matcool/gd-imgui-cocos](https://github.com/matcool/gd-imgui-cocos)
(ImGui rendered through cocos2d, no graphics-API hooks).

It is vendored rather than pulled via CPM because upstream `main` no longer
compiles against current Geode bindings: `CCIMEDelegate::insertText`,
`CCLayer::keyDown` and `CCKeyboardDelegate::keyUp` gained an extra trailing
parameter, so the upstream 2-argument `override`s are ill-formed.

## Local patch

Only three signatures were changed, in both `include/imgui-cocos.hpp` and
`src/main.cpp`; the method bodies are unchanged and simply ignore the added
argument:

- `insertText(const char*, int)` -> `insertText(const char*, int, cocos2d::enumKeyCodes)`
- `keyDown(cocos2d::enumKeyCodes)` -> `keyDown(cocos2d::enumKeyCodes, double)`
- `keyUp(cocos2d::enumKeyCodes)` -> `keyUp(cocos2d::enumKeyCodes, double)`

`glew32.lib` is the import library upstream ships for the Windows OpenGL
symbols used by the renderer; it is kept as-is.

Everything is wired up from the root `CMakeLists.txt`.
