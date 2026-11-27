# Compositor 插件开发指南

Compositor 支持用 **C/C++ 动态库**编写原生插件。插件可以读取文档与图层信息、修改图层属性、读写像素，并可调用软件内置命令（因此插件改动同样可撤销）。

## 快速开始

### 1. 插件是一个 DLL

插件导出唯一符号 `CompositorPluginEntry`，头文件为 [`include/compositor_plugin.h`](../include/compositor_plugin.h)：

```c
#include "compositor_plugin.h"

static const compositor_host_api* g_host;

static compositor_result my_get_info(void* user, compositor_plugin_info* out) {
    (void)user;
    out->abi_version = COMPOSITOR_PLUGIN_ABI_VERSION;  // 必填
    out->id = "com.example.hello";
    out->name = "示例插件";
    out->version = "1.0.0";
    out->vendor = "Your Name";
    out->description = "一句话说明";
    return COMPOSITOR_OK;
}

static compositor_result my_create(void* user) { (void)user; return COMPOSITOR_OK; }
static void my_destroy(void* user) { (void)user; }

static compositor_result my_run(void* user) {
    (void)user;
    compositor_document_info info;
    if (g_host->get_document_info(g_host->host_data, &info) == COMPOSITOR_OK) {
        g_host->log(g_host->host_data, "文档已打开");
    }
    return COMPOSITOR_OK;
}

static const compositor_plugin_api g_api = { 0, my_get_info, my_create, my_destroy, my_run };

__declspec(dllexport) const compositor_plugin_api*
CompositorPluginEntry(const compositor_host_api* host) {
    g_host = host;
    return &g_api;
}
```

### 2. 编译

```powershell
cl /LD /utf-8 /I<仓库>\include my_plugin.c /link /OUT:my_plugin.compositor-plugin.dll
```

> `/utf-8` 是必须的：否则源码里的中文会被按 GBK 解析并编译失败。

### 3. 安装

把 DLL 放进 **`Compositor.exe` 同目录的 `plugins/` 文件夹**，重启程序即可。
菜单栏末尾会出现 **插件** 菜单，每个插件占一项。

## API 速查

### 文档

| 函数 | 说明 |
|---|---|
| `get_document_info` | 宽、高、DPI、图层总数、活动图层索引、标题 |

### 图层

| 函数 | 说明 |
|---|---|
| `get_layer_count` | 图层数量 |
| `get_layer_info` | 单个图层的 id、名称、可见性、是否组、有无蒙版、混合模式、不透明度、父图层 |
| `set_layer_opacity` | 设置不透明度（0..1） |
| `set_layer_visible` | 显示/隐藏 |
| `set_layer_blend_mode` | 设置混合模式（索引见下） |
| `create_layer` | 新建空图层 |
| `delete_layer` | 删除图层 |

### 像素

| 函数 | 说明 |
|---|---|
| `read_layer_pixels` | 读取为 RGBA8，用完必须调用 `free_pixels` |
| `write_layer_pixels` | 写入 RGBA8 |
| `free_pixels` | 释放 `read_layer_pixels` 返回的缓冲 |

### 宿主

| 函数 | 说明 |
|---|---|
| `run_command` | 按命令 ID 执行内置命令，如 `"edit.undo"`、`"file.export"` |
| `log` | 写日志（输出到 stderr） |

### 混合模式索引

`0` Normal、`1` Multiply、`2` Screen、`3` Overlay …完整顺序与 `src/core/Document.h` 中的 `blendNames` 一致（共 24 个）。

## 约定与限制

- **可撤销**：所有修改都经过编辑器的编辑路径，会进入撤销栈。
- **字符串生命周期**：`get_layer_info` 返回的 `id`/`name` 指针只在**当前这次回调内**有效，需要保留请自行 `strdup`。
- **不要跨线程**调用宿主函数，必须在 `run()` 内完成。
- **ABI 校验**：`abi_version` 不匹配的插件会被拒绝加载并记录日志。
- **异常隔离**：宿主不会向插件抛异常；插件也不应让异常跨越边界。

## 与 Photoshop UXP 的关系

PS 的 UXP/CEP 插件**不能直接在本软件中运行**，原因是：

- UXP 插件依赖 Photoshop 内嵌的 JS 运行时与 `photoshop.*` 宿主 API；
- CEP 插件依赖宿主提供的 `CSInterface`/ExtendScript 桥接。

两者都不是独立进程，必须由宿主实现完整的 API。本软件不是 PS 的宿主，因此无法原生运行这些插件。

仓库提供 `tools/uxp-translate/` 转译工具，可把 UXP 插件的 `manifest.json` 和常见 API 调用**静态翻译**为这里的 C 插件骨架，并**明确列出无法翻译的 API**，便于人工移植。

## 当前不支持

以下 UXP 能力在 v1 SDK 中**没有对应实现**，转译工具会报告为不支持：

- `batchPlay` 动作描述符（PS 的动作系统，数百条 descriptor）
- UXP 的 HTML/CSS 面板 UI
- 智能对象、3D、视频时间轴、Adobe 云文档
