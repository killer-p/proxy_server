# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目概述

这是一个 mihomo (Clash Meta) 代理管理工具，包含两个入口：

- **clash-ctl** (`src/clash-ctl.c`)：C 语言编写的 CLI 控制工具，零外部依赖
- **clash-web** (`clash-web/`)：Python 编写的 Web 管理界面，零外部依赖（纯标准库）

```
proxy_server/
├── src/clash-ctl.c      # C CLI 源码
├── lib/cjson/           # cJSON 源码（MIT 单文件，纳入版本控制）
├── bin/                 # 编译产物 + mihomo 二进制 + 运行时文件
│   ├── clash-ctl        # 可执行文件
│   ├── mihomo           # 代理核心
│   ├── proxy.txt        # 生成的 mihomo 配置
│   ├── .clash-url       # 订阅链接
│   └── clash.log        # 运行日志
├── Makefile
├── clash-web/
│   ├── server.py        # Python 后端（标准库，无依赖）
│   └── static/index.html # 单页前端（内联 HTML/CSS/JS）
└── test/test_run.sh     # 自动化测试脚本
```

## 常用命令

### 编译
```bash
make          # 编译 clash-ctl
make deploy   # 编译 + 设置权限
make clean    # 清理编译产物
```

### clash-ctl CLI（在 bin/ 目录执行）
```bash
./clash-ctl set-url <订阅链接>  # 设置订阅
./clash-ctl start               # 启动代理
./clash-ctl stop                # 停止代理
./clash-ctl status              # 查看状态 + 实时流量（Ctrl+C 退出）
./clash-ctl list                # 列出所有节点
./clash-ctl select <节点名>     # 切换节点
./clash-ctl select <编号>       # 按编号切换节点
./clash-ctl update              # 从订阅更新配置
./clash-ctl update-geo           # 更新 GeoIP 数据库
```

### clash-web Web 界面
```bash
cd clash-web
python3 server.py   # 前台运行，访问 http://localhost:8080
```

### 测试
```bash
./test/test_run.sh                      # 使用默认订阅链接测试
SUB_URL=<链接> ./test/test_run.sh      # 指定订阅链接测试
```

## 架构要点

### mihomo REST API (127.0.0.1:9090)

mihomo 仓库地址：https://github.com/MetaCubeX/mihomo
mihomo api在线api文档：https://wiki.metacubex.one/api/

| 操作 | 方法 | 路径 |
|------|------|------|
| 获取节点列表 | GET | `/proxies` |
| 获取当前节点 | GET | `/proxies` → 解析 `FINAL.now` |
| 切换 provider 节点 | PUT | `/providers/proxies/sub/select` |
| 切换 Manual 组 | PUT | `/proxies/Manual` |
| 实时流量 | GET | `/traffic`（SSE 流，每秒一行 JSON） |

### proxy-provider 机制

clash-ctl **不解析订阅**，只生成包含 `proxy-provider type: http` 的配置文件，由 mihomo 自动完成：
- 下载订阅内容
- 检测并解码 base64 / URI / YAML 格式
- 缓存节点到 `profiles/sub.yaml`
- 定时自动更新（interval: 3600）

### 生成的 proxy.txt 结构
```yaml
proxy-providers:
  sub:
    type: http
    url: "<订阅URL>"
    path: profiles/sub.yaml
    health-check:
      enable: true
      url: http://www.gstatic.com/generate_204

proxy-groups:
  - name: 自动选择   # URLTest，自动测速
  - name: Manual     # Selector，手动选择
  - name: FINAL     # Selector，汇总出口
    proxies:
      - 自动选择
      - Manual
      - DIRECT
```

### /traffic SSE 特性（重要）

mihomo `/traffic` 返回的是 **chunked transfer encoding**，每行一个 JSON，永不关闭连接：
- `HTTPResponse.read()` 会在 8192 字节前阻塞（不达到 8192 就不会返回）
- 必须用 `readline()` 逐行读取
- Content-Type 是 `application/json`，不能靠 Content-Type 判断，需按 path 判断

### socket 超时处理

clash-ctl 使用 POSIX `O_NONBLOCK` + `select()` 实现超时（无外部库依赖）：
- 连接超时：5 秒
- 接收超时：通过 `select()` 控制

### 节点延迟数据来源

mihomo 节点延迟来自 `extra['http://www.gstatic.com/generate_204'].history[0].delay`，是 mihomo 自身的健康检查结果，完全可信。

### clash-web Python 后端特性

- **零外部依赖**：只用 Python 标准库（`http.server`, `http.client`, `subprocess`）
- **路由顺序敏感**：`/api/status`、`/api/start` 等具体路由必须写在 `/api/` 泛代理路由之前
- **SSE 处理**：`proxy_mihomo()` 中对 `/traffic` 使用 `readline()` 而非 `read()`
- **错误日志**：写到 `stderr`，前台运行可见

## clash-ctl 源码结构

```
clash-ctl.c
├── 配置常量（API_HOST, API_PORT, CLASH_CONFIG 等）
├── 工具函数（trim, exec_cmd, format_bytes, print_*)
├── http_request()         # HTTP/TCP 请求，支持 chunked encoding
├── get_clash_pid()        # 通过 pgrep 查找 mihomo 进程
├── get_subscription_url()  # 读取 .clash-url 文件
├── cmd_set_url / cmd_show_url / cmd_update   # 订阅管理
├── cmd_update_geo()       # 下载 GeoIP 数据库
├── read_log_tail() / scan_log_errors()      # 日志扫描（cmd_start 健康检查用）
├── cmd_start()            # 启动 + 三重健康检查（日志扫描 + API + provider节点数）
├── cmd_stop() / cmd_restart()
├── cmd_status()           # 显示当前节点 + 调用 traffic_monitor()
├── traffic_monitor()      # select() 循环读取 /traffic，Ctrl+C 退出
├── cmd_list() / cmd_select() / get_node_name_by_index()
└── main()                 # 命令分发
```
