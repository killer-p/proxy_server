# Clash 局域网代理控制工具

纯 C 语言编写的轻量级代理控制 CLI，无需任何外部依赖（无 libcurl、json 库等）。

## 目录结构

```
proxy_server/
├── src/            # 源码
│   └── clash-ctl.c
├── lib/            # 第三方库
│   └── cjson/      # cJSON 源码
├── app/            # 编译产物 + 运行时文件（所有程序统一放在此目录）
│   ├── clash-ctl   # CLI 控制工具
│   ├── mihomo      # 代理核心
│   ├── Country.mmdb # GeoIP 数据库
│   ├── server.py   # Web 管理界面（Python 标准库，零依赖）
│   ├── clash-web/static/  # 前端文件
│   └── clash.log   # 运行日志
├── Makefile
├── test/           # 自动化测试脚本
│   └── test_run.sh
├── clash-fnos/     # 飞牛 fnOS 打包配置（make fnpack 打包用）
│   ├── manifest
│   ├── app/        # 打包时同步 app/ 下的文件
│   ├── cmd/        # fnOS 生命周期脚本（main/start/stop）
│   ├── config/     # fnOS 权限配置
│   └── wizard/     # fnOS 安装向导
└── README.md
```

> `profiles/`（订阅节点缓存）、`proxy.txt`（mihomo 配置文件）、`.clash-url`（订阅链接）运行时生成在 `app/` 目录中。

## 快速开始

### 1. 编译（可选）

```bash
make
```

### 2. 配置订阅

```bash
cd app
./clash-ctl set-url <订阅链接>
```

### 3. 启动 CLI 方式

```bash
./clash-ctl start
```

### 4. Web 界面方式

```bash
python3 app/server.py        # 前台运行，http://localhost:8567
```

**系统要求**：Python 3（标准库，无外部依赖）

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
| `./clash-ctl status` | 查看当前选中节点 + 实时流量（Ctrl+C 退出） |
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

## Web 管理界面

提供图形化 Web 界面，功能与 `clash-ctl` CLI 等价：

- 启动 / 停止代理服务
- 订阅链接管理
- 节点切换（支持节点延迟显示）
- 实时流量监控

启动方式：
```bash
python3 app/server.py                    # 默认端口 8567
python3 app/server.py --port 8080       # 指定端口
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

## 飞牛 fnOS 部署

使用 `make fnpack` 打包，上传到 fnOS 应用中心安装：

```bash
make fnpack    # 生成 clash-fnos/fnnas.clash-proxy.fpk
```

安装后：
- 应用中心"启动"按钮启动 server.py
- 点击桌面图标打开 Web 管理界面
- 在 Web 界面配置订阅链接、启动/停止代理

fnOS 上的目录结构：
```
/var/apps/fnnas.clash-proxy/target/   ← TRIM_APPDEST
├── clash-ctl
├── mihomo
├── server.py
├── clash-web/static/
└── ...
```

## 使用示例

```bash
cd app

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

## 进阶

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

### 3. cJSON 解析 API 响应

clash-ctl 通过 cJSON（MIT 协议，单文件源码）解析 mihomo REST API 返回的 JSON：

```c
cJSON *root = cJSON_Parse(json);
cJSON *proxies = cJSON_GetObjectItem(root, "proxies");
for (cJSON *item = proxies->child; item != NULL; item = item->next) {
    cJSON *type = cJSON_GetObjectItem(item, "type");
    if (strcmp(type->valuestring, "Selector") == 0) { ... }
}
cJSON_Delete(root);  // 记得释放
```

无需任何外部库，cJSON 源码直接放在 `lib/` 目录下。

### 4. REST API 通信

mihomo 监听 `127.0.0.1:9090`，clash-ctl 通过 HTTP API 控制：

| 操作 | 方法 | 路径 |
|------|------|------|
| 获取节点列表 | GET | `/proxies` |
| 获取当前节点 | GET | `/proxies` (解析 FINAL.now) |
| 切换节点 | PUT | `/providers/proxies/sub/select` |
| 切换节点 | PUT | `/proxies/Manual` |

### 5. 进程管理

```c
// 启动后台进程
nohup ./mihomo -d . -f proxy.txt > clash.log 2>&1 &

// 查找进程
pid = get_clash_pid();  // 通过 pgrep 实现

// 停止进程
kill(pid, SIGTERM);
```

### 6. HTTP Chunked Encoding 解析

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

### 7. 导入订阅节点文件

将你自己的节点订阅文件 xxx.yaml 拷贝到 `app/profiles/sub.yaml`

## 维护说明

### 更新订阅

```bash
cd app
./clash-ctl set-url <新链接>   # 保存链接
./clash-ctl update            # 更新配置
./clash-ctl restart            # 重启生效
```

### 更新 GeoIP

```bash
cd app
./clash-ctl update-geo
./clash-ctl restart
```

### 查看日志

```bash
tail -f app/clash.log
```

### 自动化测试

使用测试脚本验证完整流程：

```bash
# 方式一：命令行传入订阅链接
./test/test_run.sh <订阅链接>

# 方式二：在脚本顶部编辑 SUB_URL 后直接执行
./test/test_run.sh

# 方式三：环境变量传入
SUB_URL=<订阅链接> ./test_run.sh
```

测试流程：编译 → 设置订阅 → 启动 → Google 连通性测试 → 停止。每一步独立记录日志到 `test/logs/` 目录，结束后生成汇总报告。

## 注意事项

- 请遵守当地法律法规使用代理服务
- 妥善保管代理配置文件，不要泄露
- 建议使用 HTTPS 健康检查 URL 以提高可靠性
