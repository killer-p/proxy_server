# Clash 代理管理器

纯 C 语言 CLI + Web 界面管理 mihomo 代理，无需任何外部依赖。

## 目录结构

```
proxy_server/
├── app/                    # 所有运行时文件
│   ├── clash-ctl          # CLI 控制工具
│   ├── mihomo             # 代理核心（二进制）
│   ├── server.py          # Web 管理界面
│   ├── clash-web/static/  # 前端文件
│   ├── Country.mmdb       # GeoIP 数据库
│   └── clash.log          # 运行日志
├── src/clash-ctl.c        # CLI 源码
├── lib/cjson/             # JSON 解析库（cJSON）
├── Makefile
└── clash-fnos/            # 飞牛 fnOS 打包配置
```

运行时生成文件：`app/proxy.txt`（mihomo 配置）、`app/.clash-url`（订阅链接）、`app/profiles/`（节点缓存）。

## 快速开始

```bash
# 1. 编译（如已编译可跳过）
make

# 2. 设置订阅链接
cd app
./clash-ctl set-url https://your-subscription-url

# 3. 启动代理
./clash-ctl start

# 4. 查看节点
./clash-ctl list
# 输出示例：
#    1. 🇭🇰 香港 01
#    2. 🇯🇵 日本 02
#    ...

# 5. 切换节点（按编号或名称）
./clash-ctl select 5
./clash-ctl select "🇭🇰 香港 01"
```

## CLI 命令

### 服务控制

| 命令 | 说明 |
|------|------|
| `./clash-ctl start` | 启动代理服务 |
| `./clash-ctl stop` | 停止代理服务 |
| `./clash-ctl restart` | 重启代理服务 |

### 节点管理

| 命令 | 说明 |
|------|------|
| `./clash-ctl list` | 列出所有可用节点 |
| `./clash-ctl select <编号>` | 按编号切换节点 |
| `./clash-ctl select <节点名>` | 按名称切换节点 |
| `./clash-ctl status` | 查看当前节点 + 实时流量（Ctrl+C 退出） |

### 订阅管理

| 命令 | 说明 |
|------|------|
| `./clash-ctl set-url <链接>` | 设置订阅链接 |
| `./clash-ctl show-url` | 显示当前订阅链接 |
| `./clash-ctl update` | 手动更新配置 |
| `./clash-ctl update-geo` | 更新 GeoIP 数据库 |

## Web 界面

图形化管理界面，功能与 CLI 等价：

- 启动 / 停止代理服务
- 订阅链接管理
- 节点切换（显示节点延迟）
- 实时流量监控

启动：
```bash
python3 app/server.py                    # 默认端口 8567
python3 app/server.py --port 8080       # 指定端口
```

访问 `http://服务器IP:8567`

## 局域网设备配置

**Windows / macOS / Linux 图形界面：**
```
设置 → 网络/系统代理 → 手动代理服务器
地址: <服务器IP>
端口: 7890
类型: HTTP
```

**Linux 命令行：**
```bash
export http_proxy="http://服务器IP:7890"
export https_proxy="http://服务器IP:7890"
```

**Android / iOS：** 在代理 App 中配置 HTTP 代理，地址同上。

## 飞牛 fnOS 部署

打包并安装到 fnOS：

```bash
make fnpack    # 生成 clash-fnos/fnnas.clash-proxy.fpk
```

上传到 fnOS 应用中心安装后：
- 点击"启动"按钮启动 Web 服务
- 点击桌面图标打开管理界面
- 在 Web 界面配置订阅并启动代理

## 技术原理

### 组件协作

```
用户操作（CLI / Web）
    │
    ▼
clash-ctl / server.py  ← 纯 C / 纯 Python，无外部依赖
    │                      通过 POSIX Socket / HTTP 与 mihomo 通信
    ▼
mihomo                     代理核心，监听 127.0.0.1:9090（控制）
    │                      监听 0.0.0.0:7890（HTTP/SOCKS5 代理）
    ▼
互联网
```

### 订阅机制

clash-ctl 不解析订阅，只生成含 `proxy-provider type: http` 的配置文件，由 mihomo 自动完成：
- 下载订阅内容
- 自动检测并解码 base64 / URI / YAML 格式
- 缓存节点到 `profiles/sub.yaml`

### 进程管理

clash-ctl 通过 `nohup` 启动 mihomo 后台进程，用 `pgrep` 查找 PID，用 `SIGTERM` 终止。无需任何进程管理库。

## 维护

```bash
cd app

# 更新订阅
./clash-ctl set-url <新链接>
./clash-ctl update
./clash-ctl restart

# 更新 GeoIP
./clash-ctl update-geo
./clash-ctl restart

# 查看日志
tail -f app/clash.log

# 自动化测试
./test/test_run.sh <订阅链接>
```

## 注意事项

- 请遵守当地法律法规使用代理服务
- 妥善保管配置文件，不要泄露
- 建议使用 HTTPS 健康检查 URL 以提高可靠性
- fnOS 部署时如需从外部设备访问代理，需在 `proxy.txt` 中将 mihomo bind 地址改为 `0.0.0.0`
