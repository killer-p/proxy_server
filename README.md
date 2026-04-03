# Clash 局域网代理控制工具

为Linux 设计的轻量级代理控制 CLI。

## 项目概述

本项目用于在 Linux 服务器上部署代理服务，使局域网内的 Windows、Linux、Mac、手机等设备可以通过简单的 HTTP 代理设置访问国际互联网。

```
┌─────────────────────────────────────────────────────┐
│              Linux 代理服务器                        │
│  ┌─────────────────┐    ┌───────────────────────┐  │
│  │   clash-ctl     │◄──►│       mihomo           │  │
│  │  (你的 C 程序)  │ HTTP│   (Clash Meta 核心)   │  │
│  └─────────────────┘    └───────────┬───────────┘  │
│                                      │               │
└──────────────────────────────────────┼───────────────┘
                                       │
                          局域网: http://服务器IP:7890
                                       │
              ┌──────────────────────────┼──────────────┐
              │                          │              │
         ┌────▼────┐  ┌──────────┐  ┌──────────┐       │
         │ Windows │  │  Linux   │  │   手机   │       │
         └─────────┘  └──────────┘  └──────────┘       │
```

## 目录结构

```
my_proxy/
├── clash-ctl       # 可执行文件（控制工具，编译后生成）
├── clash-ctl.c    # 源码
├── Makefile       # 编译配置
├── mihomo         # 代理核心（需下载，参见下方说明）
├── Country.mmdb   # GeoIP 数据库
└── proxy.txt      # 代理配置文件
```

## 快速开始

### 1. 下载 mihomo 核心

```bash
# 方法一：使用 make（推荐）
make download

# 方法二：手动下载
wget https://github.com/MetaCubeX/mihomo/releases/download/v1.19.22/mihomo-linux-amd64-compatible-v1.19.22.gz
gunzip mihomo-linux-amd64-compatible-v1.19.22.gz
mv mihomo-linux-amd64-compatible-v1.19.22 mihomo
chmod +x mihomo
```

### 2. 编译

```bash
make
```

### 3. 使用

```bash
./clash-ctl help
```

## 命令说明

### 服务控制

| 命令 | 说明 |
|------|------|
| `./clash-ctl start` | 启动代理服务 |
| `./clash-ctl stop` | 停止代理服务 |
| `./clash-ctl restart` | 重启代理服务 |

### 状态查询

| 命令 | 说明 |
|------|------|
| `./clash-ctl status` | 查看当前节点 |
| `./clash-ctl list` | 列出所有可用节点 |

### 节点管理

| 命令 | 说明 |
|------|------|
| `./clash-ctl select <节点名>` | 切换到指定节点 |

### 订阅管理

| 命令 | 说明 |
|------|------|
| `./clash-ctl set-url <链接>` | 设置订阅链接 |
| `./clash-ctl show-url` | 显示当前订阅链接 |
| `./clash-ctl update` | 从订阅更新配置 |

**注意**: 每次 `start` 时会自动检查并更新订阅配置。

### 其他

| 命令 | 说明 |
|------|------|
| `./clash-ctl update-geo` | 更新 GeoIP 数据库 |
| `./clash-ctl help` | 显示帮助 |

## 局域网设备配置

**Windows:**
```
设置 → 网络和Internet → 代理 → 手动代理服务器设置
地址: 你的Linux服务器IP
端口: 7890
```

**Linux:**
```bash
export http_proxy="http://服务器IP:7890"
export https_proxy="http://服务器IP:7890"
```

## 订阅配置示例

```bash
# 设置订阅链接
./clash-ctl set-url https://your-subscription-url

# 手动更新
./clash-ctl update

# 启动时会自动更新
./clash-ctl start
```

## 技术要点

### 1. POSIX Socket 编程

纯 C 语言实现，无外部依赖（curl、json 库等）。

```c
// 创建 socket
int sock = socket(AF_INET, SOCK_STREAM, 0);

// 连接服务器
connect(sock, (struct sockaddr *)&server, sizeof(server));

// 发送请求
send(sock, request, strlen(request), 0);

// 接收响应（支持 chunked encoding）
while ((r = recv(sock, buf, sizeof(buf)-1, 0)) > 0) {
    total += r;
}
```

### 2. HTTP API 通信

mihomo 提供 REST API：

```
GET  http://127.0.0.1:9090/proxies          # 获取所有代理信息
PUT  http://127.0.0.1:9090/proxies/<组名>  # 切换节点
```

### 3. 进程管理

```c
// 启动后台进程
nohup ./mihomo -d . -f proxy.txt > clash.log 2>&1 &

// 查找进程
pid = get_clash_pid();  // 通过 pgrep 实现

// 停止进程
kill(pid, SIGTERM);
```

### 4. HTTP Chunked Encoding

mihomo API 使用 chunked transfer encoding，响应格式：

```
HTTP/1.1 200 OK
Transfer-Encoding: chunked

321d\r\n              ← 块大小（十六进制）
{JSON 数据...}\r\n    ← 块内容
0\r\n                 ← 结束块
\r\n
```

## 代理协议

项目使用三种代理协议（由 mihomo 处理）：

| 协议 | 加密 | 特点 |
|------|------|------|
| VMess | AES-128-GCM | 常用，兼容性好 |
| VLESS | 可无加密 | 性能更好，支持 REALITY |
| Shadowsocks | AES-256-GCM | 简单高效 |

## 维护说明

### 更新代理配置

```bash
# 方式一：通过订阅
./clash-ctl update

# 方式二：手动编辑后重启
./clash-ctl restart
```

### 更新 GeoIP 数据库

```bash
./clash-ctl update-geo
```

### 日志查看

```bash
tail -f clash.log
```

## 工作原理

1. **clash-ctl** 通过 HTTP API 与 mihomo 通信，控制启动/停止/切换节点
2. **mihomo** 监听 7890 端口，接收局域网代理请求
3. 根据 **proxy.txt** 中的规则，决定直连或通过代理转发
4. 需要代理的请求通过加密隧道发送到海外服务器
5. 海外服务器代为请求目标网站，返回数据

## 适用场景

- 开发者访问 Google、GitHub、Stack Overflow 等技术资源
- 学习嵌入式 Linux 时查阅国际技术文档
- 家庭/办公室共享代理上网

## 注意事项

- 请遵守当地法律法规使用代理服务
- 妥善保管代理配置文件，不要泄露
- 定期检查代理节点可用性
