# Clash 局域网代理控制工具

为嵌入式 Linux 开发者设计的轻量级代理控制 CLI。

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
├── clash-ctl       # 可执行文件（控制工具）
├── clash-ctl.c    # 源码
├── Makefile       # 编译配置
├── mihomo         # 代理核心（第三方 Go 程序）
├── Country.mmdb   # GeoIP 数据库（判断 IP 归属地）
└── proxy.txt      # 代理配置文件
```

## 快速开始

### 编译

```bash
make
```

### 使用方法

```bash
./clash-ctl help
```

可用命令：

| 命令 | 说明 |
|------|------|
| `./clash-ctl start` | 启动代理服务 |
| `./clash-ctl stop` | 停止代理服务 |
| `./clash-ctl status` | 查看当前节点 |
| `./clash-ctl list` | 列出所有可用节点 |
| `./clash-ctl select <节点名>` | 切换到指定节点 |
| `./clash-ctl restart` | 重启代理服务 |

### 局域网设备配置

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

**或者使用环境变量脚本:**
```bash
source proxy_on.sh   # 开启代理
source proxy_off.sh  # 关闭代理
```

## 开发记录

### 主要难点及解决方案

#### 1. mihomo 无法找到 MMDB 数据库

**问题描述:**
```
Can't find MMDB, start download
```

mihomo 启动后不断尝试下载 MMDB 文件，即使 `mmdb/Country.mmdb` 存在也无法识别。

**原因分析:**
- mihomo 需要 `Country.mmdb` 文件在**程序运行目录**（即根目录）
- 不在 `mmdb/` 子目录中

**解决方案:**
```bash
# 将 Country.mmdb 放在项目根目录
cp mmdb/Country.mmdb ./Country.mmdb
./mihomo -d . -f proxy.txt
```

#### 2. HTTP chunked encoding 解析

**问题描述:**
`cmd_status()` 和 `cmd_list()` 无法正确获取当前节点信息。

**原因分析:**
mihomo API 使用 HTTP chunked transfer encoding（分块传输编码），响应格式为：

```
HTTP/1.1 200 OK
Transfer-Encoding: chunked

321d\r\n              ← 块大小（十六进制 0x321d = 12829）
{JSON 数据...}\r\n    ← 块内容
0\r\n                 ← 结束块
\r\n
```

**解决方案:**
实现完整的 chunked encoding 解析器：

```c
while (1) {
    char *line_end = strstr(p, "\r\n");
    *line_end = '\0';
    int chunk_size = (int)strtol(p, NULL, 16);  // 十六进制解析
    *line_end = '\r';

    if (chunk_size <= 0) break;

    p = line_end + 2;
    // 复制 chunk_size 字节的数据
    memcpy(result + result_len, p, chunk_size);
    result_len += chunk_size;

    p += chunk_size + 2;  // 跳过数据 + \r\n
}
```

#### 3. JSON 结构理解

**问题描述:**
使用 `strstr()` 查找 `"now"` 字段时，得到的是第一个匹配，而非目标代理组的值。

**原因分析:**
mihomo API 返回的 JSON 中，多个代理组都有 `now` 字段：

```json
{
  "proxies": {
    "GLOBAL": {"now": "DIRECT", ...},
    "自动选择": {"now": "🇯🇵日本 02 专", ...},
    "永雏塔菲的魔法卷轴": {"now": "🇸🇬新加坡 04", ...}
  }
}
```

**解决方案:**
先定位到目标代理组对象，再在该对象范围内查找 `now` 字段：

```c
// 1. 找到代理组
char *group = strstr(json, "\"永雏塔菲的魔法卷轴\"");

// 2. 确定组对象的范围
char *obj_end = strchr(group, '}');

// 3. 在组范围内查找 now
char *now = strstr(group, "\"now\"");
if (now && now < obj_end) {
    // 安全提取值
}
```

#### 4. GeoIP 数据库下载

**问题描述:**
GitHub 下载被墙，需要通过代理访问。

**解决方案:**
```bash
export http_proxy="http://代理服务器:端口"
wget https://github.com/MetaCubeX/meta-rules-dat/releases/download/latest/country.mmdb
```

## 技术要点

### 1. POSIX Socket 编程

```c
// 创建 socket
int sock = socket(AF_INET, SOCK_STREAM, 0);

// 连接服务器
connect(sock, (struct sockaddr *)&server, sizeof(server));

// 发送请求
send(sock, request, strlen(request), 0);

// 接收响应（循环读取）
while ((r = recv(sock, buf, sizeof(buf)-1, 0)) > 0) {
    total += r;
}
```

### 2. 进程管理

```c
// 启动后台进程
nohup ./mihomo -d . -f proxy.txt > /dev/null 2>&1 &

// 查找进程
pid = get_pid_by_name("mihomo.*proxy.txt");

// 停止进程
kill(pid, SIGTERM);
```

### 3. REST API 调用

```c
// GET 请求（查询状态）
GET /proxies HTTP/1.1
Host: 127.0.0.1:9090

// PUT 请求（切换节点）
PUT /proxies/永雏塔菲的魔法卷轴 HTTP/1.1
Content-Type: application/json

{"name": "🇭🇰香港 03 专"}
```

## 代理协议说明

项目使用三种代理协议（由 mihomo 处理）：

| 协议 | 加密 | 特点 |
|------|------|------|
| VMess | AES-128-GCM | 常用，兼容性好 |
| VLESS | 可无加密 | 性能更好，支持 REALITY |
| Shadowsocks | AES-256-GCM | 简单高效 |

## 维护说明

### 更新代理配置

直接编辑 `proxy.txt`，然后重启服务：

```bash
./clash-ctl restart
```

### 更新 GeoIP 数据库

```bash
wget -O Country.mmdb \
    "https://github.com/MetaCubeX/meta-rules-dat/releases/download/latest/country.mmdb"
```

### 日志查看

```bash
./mihomo -d . -f proxy.txt
# 前台运行，Ctrl+C 停止
```

## 原理简述

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
