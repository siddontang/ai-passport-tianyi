# AI Passport — 天依像素版

用于 **FoloToy AI Passport（ESP32-C3、8 MB Flash、无 PSRAM）** 的定制固件，
包含天依风格的像素头像、连续语音对话和中文字幕。
当前经过实机验收的版本为 **2.2.1-tianyi.pixel.16**。

[English](README.md) · [硬件与操作说明](firmware/boards/ai-passport/README.md) ·
[语音实现说明](firmware/components/agent_link/REALTIME.md)

<img src="firmware/apps/ai-passport-wifi/avatar/tianyi-pixel-expressions.png" width="320" alt="天依风格像素头像：待机、眨眼和两种说话表情">

## 已实现

- 64×64 像素头像、眨眼、聆听反馈与音量驱动的嘴部动画，复用 240×4 DMA 条带渲染。
- 上游 C3 Opus 音频实现、共享编解码工作区和增量 TLS 收发。
- 定时发送 WebSocket Pong，修复长语音回复期间服务端心跳超时导致的断开。
- 用户字幕即时显示；回复字幕按实际释放的 PCM 样本推进，考虑播放队列和 I2S 尾音。
- 中文字体、头像与字体生成工具、主机回归测试。

字幕是近似同步：当前协议没有逐字时间戳。字幕窗口容纳 1,024 个 UTF-16 单元，
累积更新会回收已显示的前缀；一次到达的超长文本仍可能超出窗口。
嘴部动画跟随音量，不是逐音素口型。

## 构建与烧录

先安装并激活 **ESP-IDF 5.5.4**（实机验收使用的版本）、Python 及 ESP-IDF 构建依赖：

```sh
git clone https://github.com/siddontang/ai-passport-tianyi.git
cd ai-passport-tianyi
python firmware/tools/build_firmware.py ai-passport-wifi --jobs 2
```

使用 USB 数据线连接设备并开机，确认串口后，烧录分段镜像以保留现有 NVS 设置：

```sh
idf.py -C firmware -p PORT flash monitor
```

把 `PORT` 替换成这台设备的实际串口。不要执行整片擦除或使用其他板子的分区布局。
构建产物和设备备份不随源码发布。新设备通过屏幕显示的 `AIPassport-XXXX` 热点，
访问 `http://192.168.4.1` 配网，再到 [Deotaland 控制台](https://bot.deotaland.cn) 激活并绑定 Agent。

## 使用与 Agent 设置

- 短按 OK 开始或结束通话；上、下键调整音量。
- 长按 OK 打开菜单，选择 **New conversation** 新建对话，旧对话仍然保留。
- 在控制台的 Agent 人设里设置自称。修改人设后应新建对话，已有会话可能保留旧指令。
  实机新对话已确认使用自定义名字 **Tiantian**。

头像不会改变音色。目前**尚未实现洛天依风格音色**：实际音色由服务端供应商配置决定，
当前控制台没有独立的每 Agent 音色选择器。说话风格提示词不能代替音色预设。
此仓库不包含 Agent Stack 服务端代码。

## 验证记录

pixel.16 已通过 ESP-IDF 构建及头像、字幕、平台 API、编解码生命周期、心跳和恢复测试。
用户实测确认声音、连接与中文字幕跟随正常。记录中包含一段完整的 43.36 秒回复，
连接至少保持 100.46 秒；这代表有限时长的实机验收，不代表无限时长稳定性保证。

测试命令见 [英文说明](README.md#validation)。头像预览和素材生成需要 Pillow，
字体生成另需 fontTools；已提交生成后的 C/C++ 素材，正常构建不需要重新生成。

## 来源与许可

基于 [Deotaland/Agent_link](https://github.com/Deotaland/Agent_link)，保留其至
`526674bd948946b47a4043dcd53e2f71490a16d4` 的历史。C3 音频修复吸收了上游
`9f664fc028a664a47789506802aecfb0b69b7e18` 的实现，再增加本地心跳与字幕修复。
这是个人定制版本，不是官方固件发布。保留完整上游源码以便构建，定制版本仅在
AI Passport WiFi 方案上完成验收。

源码保留上游 [MIT 许可证](LICENSE)，第三方代码和字体保留各自声明，
包括 [SIL 开源字体许可证](firmware/boards/common/fonts/OFL.txt)。
天依风格素材属于同人创作，代码许可证不授予第三方角色或商标权利。
素材原图与生成提示词位于 `firmware/apps/ai-passport-wifi/avatar/`。
Wi-Fi 凭据、设备密钥、设备备份、日志和含私有配置的固件均不上传。
