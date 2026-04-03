# Clash 局域网代理控制工具

纯 C 语言编写的轻量级代理控制 CLI，无需任何外部依赖（无 libcurl、json 库等）。

## 目录结构

```
proxy_server/
├── clash-ctl       # 可执行文件（编译后生成）
├── clash-ctl.c    # 源码
├── Makefile       # 编译配置
├── mihomo         # 代理核心
├── Country.mmdb   # GeoIP 数据库
├── proxy.txt      # 代理配置文件（自动生成）
├── .clash-url     # 订阅链接存储
└── profiles/      # mihomo 缓存目录（自动生成）
    └── sub.yaml   # 订阅节点缓存
```

## 快速开始

### 1. 编译

```bash
make
```

### 2. 配置订阅

```bash
./clash-ctl set-url <订阅链接>
```

### 3. 启动

```bash
./clash-ctl start
```

### 更新二进制文件

如需更新 mihomo 或 GeoIP 数据库，请手动下载并替换：

- mihomo: https://github.com/MetaCubeX/mihomo/releases
- Country.mmdb: https://github.com/Loyalsoldier/geoip/releases

## 命令说明

### 服务控制

| 命令 | 说明 |
|------|------|
| `./clash-ctl start` | 启动代理服务（自动更新订阅配置） |
| `./clash-ctl stop` | 停止代理服务 |
| `./clash-ctl restart` | 重启代理服务 |

### 状态查询

| 命令 | 说明 |
|------|------|
| `./clash-ctl status` | 查看当前选中节点（自动追溯真实节点） |
| `./clash-ctl list` | 列出所有可用节点（带编号） |

### 节点管理

| 命令 | 说明 |
|------|------|
| `./clash-ctl select <编号>` | 按编号切换节点 |
| `./clash-ctl select <节点名>` | 按名称切换节点 |

### 订阅管理

| 命令 | 说明 |
|------|------|
| `./clash-ctl set-url <链接>` | 设置订阅链接 |
| `./clash-ctl show-url` | 显示当前订阅链接 |
| `./clash-ctl update` | 手动更新配置文件 |

### 其他

| 命令 | 说明 |
|------|------|
| `./clash-ctl update-geo` | 更新 GeoIP 数据库 |
| `./clash-ctl help` | 显示帮助 |

## 使用示例

```bash
# 设置订阅
./clash-ctl set-url https://your-subscription-url

# 启动服务
./clash-ctl start

# 查看可用节点
./clash-ctl list
# 输出：
#    1. 节点A
#    2. 节点B
#    ...

# 按编号切换节点（推荐）
./clash-ctl select 5

# 按名称切换节点
./clash-ctl select "🇭🇰香港 03 专"

# 查看当前状态
./clash-ctl status
```

## 局域网设备配置

**Windows / macOS / Linux 图形界面:**
```
设置 → 网络/系统代理 → 手动代理服务器
地址: <服务器IP>
端口: 7890
类型: HTTP
```

**Linux 命令行:**
```bash
export http_proxy="http://服务器IP:7890"
export https_proxy="http://服务器IP:7890"
```

**Android / iOS:**
在代理 App 中配置 HTTP 代理，地址同上。

## 技术要点

### 1. 纯 POSIX Socket 实现

无任何外部库依赖，使用标准 POSIX 接口：

```c
// 创建 socket 并连接
int sock = socket(AF_INET, SOCK_STREAM, 0);
connect(sock, (struct sockaddr *)&server, sizeof(server));

// 发送 HTTP 请求
send(sock, request, strlen(request), 0);

// 接收响应（支持 chunked encoding）
while ((r = recv(sock, buf, sizeof(buf)-1, 0)) > 0) {
    buf[r] = '\0';
    total += r;
}
```

### 2. mihomo proxy-provider 机制

clash-ctl 不做订阅解析，只生成包含 `proxy-provider type: http` 的配置文件，由 mihomo 自动完成：
- 下载订阅内容
- 检测并解码 base64 / URI / YAML 格式
- 缓存节点到 `profiles/sub.yaml`
- 定时自动更新

生成的 `proxy.txt` 结构：

```yaml
proxy-providers:
  sub:
    type: http
    url: "https://..."
    interval: 3600
    path: ./profiles/sub.yaml
    health-check:
      enable: true
      url: http://www.gstatic.com/generate_204
      interval: 300

proxy-groups:
  - name: 自动选择   # URLTest，自动测速选最优
  - name: Manual     # Selector，手动选择节点
  - name: FINAL     # Selector，最终出口
    proxies:
      - 自动选择
      - Manual
      - DIRECT
```

### 3. REST API 通信

mihomo 监听 `127.0.0.1:9090`，clash-ctl 通过 HTTP API 控制：

| 操作 | 方法 | 路径 |
|------|------|------|
| 获取节点列表 | GET | `/proxies` |
| 获取当前节点 | GET | `/proxies` (解析 FINAL.now) |
| 切换节点 | PUT | `/providers/proxies/sub/select` |
| 切换节点 | PUT | `/proxies/Manual` |

### 4. 进程管理

```c
// 启动后台进程
nohup ./mihomo -d . -f proxy.txt > clash.log 2>&1 &

// 查找进程
pid = get_clash_pid();  // 通过 pgrep 实现

// 停止进程
kill(pid, SIGTERM);
```

### 5. HTTP Chunked Encoding 解析

mihomo API 响应使用 chunked transfer encoding：

```
HTTP/1.1 200 OK
Transfer-Encoding: chunked

321d\r\n              ← 块大小（十六进制）
{JSON 数据...}\r\n    ← 块内容
0\r\n                 ← 结束块
\r\n
```

clash-ctl 从响应中提取 JSON 数据块并拼接。

## 维护说明

### 更新订阅

```bash
./clash-ctl set-url <新链接>   # 保存链接
./clash-ctl update            # 更新配置
./clash-ctl restart            # 重启生效
```

### 更新 GeoIP

```bash
./clash-ctl update-geo
./clash-ctl restart
```

### 查看日志

```bash
tail -f clash.log
```

## 注意事项

- 请遵守当地法律法规使用代理服务
- 妥善保管代理配置文件，不要泄露
- 建议使用 HTTPS 健康检查 URL 以提高可靠性
