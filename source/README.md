# 源码发布范围

本目录只保留源代码、构建描述和可公开的示例配置；不包含二进制、固件镜像、压缩课程资料、实机日志、网络配置、密钥或个人学习记录。

## 天气时钟

`weather-clock/` 是 STM32F4 天气时钟工程的源代码与 Keil 项目文件。`app/app.h` 中的 Wi-Fi 配置和天气 API 访问串已替换为占位符，使用前请自行配置。

## 数码相框

`digital-photo-frame-experiments/` 包含本机可获得的 Linux 文件 I/O、FrameBuffer、图像、字体、输入与触摸实验源码。完整数码相框课程案例源码不在当前本机资料中，故不在此仓库伪造或补写。

## S 参数模拟系统

`sparameter-simulator/` 包含用户态 C 服务、字符设备/设备树参考代码、Qt 客户端、测试和工具。Qt 客户端默认目标改为 `127.0.0.1`，实际目标可在应用设置中覆盖；`config/sparam.conf.example` 是可编辑样例。
