#!/usr/bin/env python3
"""Translate a Photoshop UXP plugin into a Compositor native plugin skeleton.

This is a static translator, not an emulator. It reads a UXP plugin's
manifest.json and JavaScript entry point, maps the API calls it recognises onto
compositor_plugin.h, and reports every call it cannot translate.

It never claims a plugin "just works": anything involving batchPlay descriptors,
UXP's HTML panel UI, or document/selection features the v1 SDK does not expose is
listed as unsupported so a human can port it.

Usage:
    python uxp_translate.py <plugin-dir> [-o <output-dir>]

Outputs:
    <output>/plugin.c            generated C plugin skeleton
    <output>/report.txt          what was translated, what was not
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

# UXP API patterns -> the SDK call that replaces them, plus a comment.
# Each entry is (regex, sdk_call_template, note). Templates may use {args}.
API_MAP: list[tuple[str, str, str]] = [
    # Document queries
    (r"app\.activeDocument\b", "/* app.activeDocument */", "取活动文档：用 get_document_info"),
    (r"\.width\b", "document_info.width", "文档宽度"),
    (r"\.height\b", "document_info.height", "文档高度"),
    (r"\.resolution\b", "document_info.resolution", "文档分辨率"),
    (r"\.layers\.length\b", "host->get_layer_count(host->host_data)", "图层数量"),
    # Layer access
    (r"app\.activeDocument\.activeLayers\b", "active layer index", "活动图层：用 get_layer_info"),
    (r"\.opacity\b", "layer.opacity", "图层不透明度"),
    (r"\.visible\b", "layer.visible", "图层可见性"),
    (r"\.blendMode\b", "layer.blend_mode", "混合模式索引"),
    (r"\.name\b", "layer.name", "图层名称"),
    # Common operations
    (r"\bcreateLayer\b|\baddLayer\b", "host->create_layer(host->host_data, name, &index)", "新建图层"),
    (r"\bdeleteLayer\b|\bremoveLayer\b", "host->delete_layer(host->host_data, index)", "删除图层"),
    (r"\bexecuteAsModal\b", "/* executeAsModal */", "模态作用域：本 SDK 的调用已是同步修改，可忽略"),
    # Logging
    (r"\bconsole\.log\b|\bconsole\.warn\b|\bconsole\.error\b",
     "host->log(host->host_data, message)", "日志"),
]

# Capabilities with no v1 equivalent. Their presence is reported and the plugin
# is flagged as needing manual work.
UNSUPPORTED: list[tuple[str, str]] = [
    (r"\bbatchPlay\b", "batchPlay 动作描述符系统（PS 专有动作，数百条 descriptor）"),
    (r"require\(\s*[\"']photoshop[\"']\s*\)", "photoshop 模块整体（UXP 宿主 API）"),
    (r"require\(\s*[\"']uxp[\"']\s*\)", "uxp 模块（宿主 UI/存储/网络）"),
    (r"\bentrypoints\.[\w.]+\b", "UXP 面板入口（HTML/CSS 界面）"),
    (r"\bselection\b(?!\s*=)", "选区 API（v1 SDK 未暴露）"),
    (r"\bsmartObject\b", "智能对象"),
    (r"\bvideo\b|\btimeline\b", "视频与时间轴"),
]


@dataclass
class Result:
    translated: list[tuple[str, str]] = field(default_factory=list)
    unsupported: list[str] = field(default_factory=list)
    manifest_notes: list[str] = field(default_factory=list)


def read_manifest(plugin_dir: Path, result: Result) -> dict:
    manifest_path = plugin_dir / "manifest.json"
    if not manifest_path.exists():
        result.manifest_notes.append("未找到 manifest.json，按无清单处理。")
        return {}
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        result.manifest_notes.append(f"manifest.json 解析失败：{error}")
        return {}
    app = manifest.get("host", [{}])
    hosts = [item.get("app", "?") for item in app if isinstance(item, dict)]
    if hosts and "PS" not in hosts:
        result.manifest_notes.append(f"该清单的目标宿主是 {hosts}，不是 Photoshop；转译结果仅供参考。")
    return manifest


def pick_entry(manifest: dict, plugin_dir: Path) -> Path | None:
    main = manifest.get("main")
    if isinstance(main, str):
        candidate = plugin_dir / main
        if candidate.exists():
            return candidate
    for name in ("index.js", "main.js", "plugin.js"):
        candidate = plugin_dir / name
        if candidate.exists():
            return candidate
    js_files = sorted(plugin_dir.rglob("*.js"))
    return js_files[0] if js_files else None


def analyse(source: str, result: Result) -> None:
    seen: set[str] = set()
    for pattern, replacement, note in API_MAP:
        for match in re.finditer(pattern, source):
            text = match.group(0)
            if text in seen:
                continue
            seen.add(text)
            result.translated.append((text, f"{replacement}  # {note}"))
    for pattern, description in UNSUPPORTED:
        if re.search(pattern, source):
            if description not in result.unsupported:
                result.unsupported.append(description)


def plugin_id(manifest: dict, plugin_dir: Path) -> str:
    raw = manifest.get("id") or plugin_dir.name
    cleaned = re.sub(r"[^A-Za-z0-9._-]", "-", str(raw))
    return cleaned or "com.example.ported"


def c_identifier(name: str) -> str:
    cleaned = re.sub(r"[^A-Za-z0-9_]", "_", name)
    if not cleaned or cleaned[0].isdigit():
        cleaned = "p_" + cleaned
    return cleaned


def render_skeleton(identifier: str, manifest: dict, result: Result) -> str:
    name = manifest.get("name", identifier)
    version = manifest.get("version", "1.0.0")
    vendor = "Ported from UXP"
    notes = "\n".join(f"//   - {item}" for item in result.unsupported) or "//   (无)"
    return f"""// Generated by uxp_translate.py from a Photoshop UXP plugin.
// Build: cl /LD /utf-8 /I<repo>\\include plugin.c /link /OUT:{identifier}.compositor-plugin.dll
//
// Unsupported capabilities detected in the source (must be ported by hand):
{notes}

#include "compositor_plugin.h"

static const compositor_host_api* g_host;

static compositor_result {identifier}_get_info(void* user, compositor_plugin_info* out) {{
    (void)user;
    out->abi_version = COMPOSITOR_PLUGIN_ABI_VERSION;
    out->id = "{identifier}";
    out->name = "{name}";
    out->version = "{version}";
    out->vendor = "{vendor}";
    out->description = "Ported from a Photoshop UXP plugin.";
    return COMPOSITOR_OK;
}}

static compositor_result {identifier}_create(void* user) {{ (void)user; return COMPOSITOR_OK; }}
static void {identifier}_destroy(void* user) {{ (void)user; }}

static compositor_result {identifier}_run(void* user) {{
    (void)user;
    if (!g_host) return COMPOSITOR_ERROR_UNKNOWN;
    compositor_document_info document_info;
    if (g_host->get_document_info(g_host->host_data, &document_info) != COMPOSITOR_OK) {{
        g_host->log(g_host->host_data, "没有打开的文档");
        return COMPOSITOR_ERROR_NO_DOCUMENT;
    }}
    // TODO: port the original plugin logic here.
    g_host->log(g_host->host_data, "ported plugin ran");
    return COMPOSITOR_OK;
}}

static const compositor_plugin_api g_api = {{
    0, {identifier}_get_info, {identifier}_create, {identifier}_destroy, {identifier}_run,
}};

__declspec(dllexport) const compositor_plugin_api*
CompositorPluginEntry(const compositor_host_api* host) {{
    g_host = host;
    return &g_api;
}}
"""


def render_report(identifier: str, result: Result) -> str:
    lines = [f"UXP 转译报告：{identifier}", "=" * 40, ""]
    lines.append("【可自动映射】")
    if result.translated:
        for text, replacement in result.translated:
            lines.append(f"  {text}")
            lines.append(f"      -> {replacement}")
    else:
        lines.append("  （未发现可映射的调用）")
    lines.append("")
    lines.append("【无法自动翻译，需人工移植】")
    if result.unsupported:
        for item in result.unsupported:
            lines.append(f"  - {item}")
    else:
        lines.append("  （未发现）")
    lines.append("")
    if result.manifest_notes:
        lines.append("【清单说明】")
        for note in result.manifest_notes:
            lines.append(f"  - {note}")
    lines.append("")
    lines.append("注意：本工具只做静态翻译，不保证移植后的插件与 Photoshop 行为一致。")
    return "\n".join(lines)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="Translate a Photoshop UXP plugin to a Compositor plugin skeleton.")
    parser.add_argument("plugin_dir", type=Path, help="UXP plugin directory")
    parser.add_argument("-o", "--output", type=Path, default=None, help="output directory")
    args = parser.parse_args(argv)

    if not args.plugin_dir.is_dir():
        print(f"错误：{args.plugin_dir} 不是目录", file=sys.stderr)
        return 2

    result = Result()
    manifest = read_manifest(args.plugin_dir, result)
    entry = pick_entry(manifest, args.plugin_dir)
    if entry is None:
        print("警告：未找到 JavaScript 入口文件，只生成空骨架。", file=sys.stderr)
        source = ""
    else:
        source = entry.read_text(encoding="utf-8", errors="replace")
    analyse(source, result)

    identifier = c_identifier(plugin_id(manifest, args.plugin_dir))
    output = args.output or (args.plugin_dir / "compositor-port")
    output.mkdir(parents=True, exist_ok=True)
    (output / "plugin.c").write_text(render_skeleton(identifier, manifest, result), encoding="utf-8")
    (output / "report.txt").write_text(render_report(identifier, result), encoding="utf-8")

    print(f"已生成 {output / 'plugin.c'}")
    print(f"已生成 {output / 'report.txt'}")
    print(f"可映射调用 {len(result.translated)} 项，不支持 {len(result.unsupported)} 项。")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
