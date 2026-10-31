<!-- SPDX-License-Identifier: AGPL-3.0-only -->
# 第三方组件声明（THIRD_PARTY_NOTICES）

MORPHO-PHS 本身以 **GNU AGPL v3（AGPL-3.0-only）** 发布，全文见 [`LICENSE`](LICENSE)。二进制（VST3 / CLAP / Standalone / `morpho_cli`）包含或链接了以下第三方组件；它们保留各自的许可证。详细的许可证分析见 `docs/LICENSES_phase4.md`。

| 组件 | 版本 | 许可证 | 用途 | 许可证文件 |
|---|---|---|---|---|
| JUCE | 8.0.9 | AGPLv3（以其 AGPLv3 选项使用）/ 商业许可 | 插件框架、编辑器、CLI，静态链接 | `third_party/JUCE/LICENSE.md` |
| Steinberg VST3 SDK（随 JUCE 提供） | 随 JUCE 8.0.9 | Steinberg VST3 License 或 GPLv3 | VST3 封装 | `third_party/JUCE/modules/juce_audio_processors/format_types/VST3_SDK/LICENSE.txt` |
| clap-juce-extensions | 7adee3a | MIT | CLAP 封装 | `third_party/clap-juce-extensions/LICENSE.md` |
| CLAP SDK / clap-helpers（子模块） | 1.2.7 / a61bcdf | MIT | CLAP 头文件 | `third_party/clap-juce-extensions/clap-libs/*/LICENSE` |
| IBM Plex Sans Condensed（Medium、SemiBold） | 随 IBM Plex 发布 | SIL Open Font License 1.1，保留字体名 “Plex” | 界面字体，未修改、以 BinaryData 嵌入 | `plugin/Assets/fonts/IBMPlex-OFL.txt` |
| IBM Plex Mono（Regular、Medium） | 随 IBM Plex 发布 | SIL Open Font License 1.1，保留字体名 “Plex” | 数值读数字体，未修改、以 BinaryData 嵌入 | `plugin/Assets/fonts/IBMPlex-OFL.txt` |

说明：

- 字体按 OFL 1.1 第 2 条随软件捆绑分发；字体文件未改名、未修改，因此不涉及“保留字体名”限制。OFL 不影响使用这些字体的软件本身的许可证。
- 标志、字标、图标与所有界面图形均为本项目原创，由 `scripts/brand/gen_brand.py` 和 `plugin/Source/ui/` 中的代码生成，属于 AGPLv3 覆盖的第一方作品。
- pluginval 仅作为验证工具使用，不随产品分发。
