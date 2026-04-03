/*
 * clash-ctl.c - Clash Meta 控制工具 (纯 POSIX 实现，无外部依赖)
 *
 * 功能：
 *   - 启动/停止 Clash Meta 代理服务
 *   - 查看代理节点列表和状态
 *   - 切换代理节点
 *
 * 编译：
 *   gcc -o clash-ctl clash-ctl.c
 *
 * 使用：
 *   ./clash-ctl start              - 启动代理
 *   ./clash-ctl stop               - 停止代理
 *   ./clash-ctl status             - 查看状态
 *   ./clash-ctl list               - 列出所有节点
 *   ./clash-ctl select <节点名>    - 选择节点
 *   ./clash-ctl restart            - 重启服务
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <time.h>
#include <ctype.h>

/* ====== 配置常量 ====== */
#define CLASH_API_HOST   "127.0.0.1"
#define CLASH_API_PORT   9090
#define CLASH_CONFIG     "proxy.txt"
#define CLASH_BIN        "./mihomo"
#define CLASH_LOG_FILE   "clash.log"
#define SUBSCRIBE_FILE   ".clash-url"
#define GEO_DB_URL       "https://github.com/MetaCubeX/meta-rules-dat/releases/download/latest/country.mmdb"

/* ====== 全局变量 ====== */
int api_port = CLASH_API_PORT;

/*
 * 工具函数：打印彩色信息
 */
void print_info(const char *msg)  { printf("\033[1;34m[信息]\033[0m %s\n", msg); }
void print_ok(const char *msg)    { printf("\033[1;32m[ OK ]\033[0m %s\n", msg); }
void print_err(const char *msg)   { printf("\033[1;31m[错误]\033[0m %s\n", msg); }
void print_warn(const char *msg)  { printf("\033[1;33m[警告]\033[0m %s\n", msg); }

/*
 * 工具函数：去除字符串首尾空白
 */
void trim(char *str)
{
    char *start = str;
    while (isspace((unsigned char)*start)) start++;

    char *end = start + strlen(start) - 1;
    while (end > start && isspace((unsigned char)*end)) end--;
    *(end + 1) = '\0';

    if (start != str) memmove(str, start, strlen(start) + 1);
}

/*
 * 工具函数：执行 shell 命令并获取输出
 * 返回: malloc 分配的字符串，需要手动 free
 */
char* exec_cmd(const char *cmd)
{
    FILE *fp = popen(cmd, "r");
    if (!fp) return NULL;

    char buf[1024];
    size_t len = 0;
    char *output = malloc(1);
    output[0] = '\0';

    while (fgets(buf, sizeof(buf), fp)) {
        output = realloc(output, len + strlen(buf) + 1);
        strcpy(output + len, buf);
        len += strlen(buf);
    }
    pclose(fp);

    if (len > 0 && output[len-1] == '\n') output[len-1] = '\0';
    return output;
}

/*
 * HTTP 请求：向 Clash API 发送请求
 * method: GET 或 PUT
 * path: API 路径
 * body: PUT 请求的 JSON 数据 (可为 NULL)
 * response: 响应数据 (输出参数，调用者需 free)
 *
 * 返回: HTTP 状态码，-1 表示网络错误
 */
int http_request(const char *method, const char *path, const char *body, char **response)
{
    int sock;
    struct sockaddr_in server;
    struct hostent *he;
    char request[4096];

    /* 创建 socket */
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return -1;
    }

    /* 设置服务器地址 */
    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons(api_port);

    if (inet_pton(AF_INET, CLASH_API_HOST, &server.sin_addr) != 1) {
        he = gethostbyname(CLASH_API_HOST);
        if (!he) {
            close(sock);
            return -1;
        }
        memcpy(&server.sin_addr, he->h_addr_list[0], he->h_length);
    }

    if (connect(sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
        close(sock);
        return -1;
    }

    /* 构造 HTTP 请求 */
    if (body && strlen(body) > 0) {
        snprintf(request, sizeof(request),
            "%s %s HTTP/1.1\r\n"
            "Host: %s:%d\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: %zu\r\n"
            "Connection: close\r\n"
            "\r\n"
            "%s",
            method, path, CLASH_API_HOST, api_port, strlen(body), body);
    } else {
        snprintf(request, sizeof(request),
            "%s %s HTTP/1.1\r\n"
            "Host: %s:%d\r\n"
            "Connection: close\r\n"
            "\r\n",
            method, path, CLASH_API_HOST, api_port);
    }

    if (send(sock, request, strlen(request), 0) < 0) {
        close(sock);
        return -1;
    }

    /* 接收响应（支持 chunked encoding） */
    char *full_response = malloc(1);
    size_t total_len = 0;
    char buf[8192];
    int status_code = 0;

    while (1) {
        int recv_len = recv(sock, buf, sizeof(buf) - 1, 0);
        if (recv_len <= 0) break;

        full_response = realloc(full_response, total_len + recv_len + 1);
        memcpy(full_response + total_len, buf, recv_len);
        total_len += recv_len;
        full_response[total_len] = '\0';
    }
    close(sock);

    if (total_len == 0) {
        free(full_response);
        return -1;
    }

    /* 解析 HTTP 响应 */
    char *header_end = strstr(full_response, "\r\n\r\n");
    if (!header_end) {
        free(full_response);
        return -1;
    }

    /* 解析状态码 */
    if (sscanf(full_response, "HTTP/1.%*d %d", &status_code) != 1) {
        free(full_response);
        return -1;
    }

    /* 检查 Transfer-Encoding: chunked */
    int is_chunked = (strstr(full_response, "Transfer-Encoding: chunked") != NULL);

    char *body_start = header_end + 4;
    char *resp_body;

    if (is_chunked) {
        /* 处理 chunked encoding */
        char *result = malloc(1);
        size_t result_len = 0;
        char *p = body_start;

        while (1) {
            /* 查找 chunk size 行（到 \r\n） */
            char *line_end = strstr(p, "\r\n");
            if (!line_end) break;

            /* 读取 chunk size（十六进制） */
            int chunk_size = (int)strtol(p, NULL, 16);
            if (chunk_size <= 0) break;

            p = line_end + 2;  /* 跳过 \r\n */
            char *chunk_data = p;
            char *next_chunk = p + chunk_size;

            result = realloc(result, result_len + chunk_size + 1);
            memcpy(result + result_len, chunk_data, chunk_size);
            result_len += chunk_size;
            result[result_len] = '\0';

            p = next_chunk;  /* 移动到下一个 chunk */
            if (strncmp(p, "\r\n", 2) == 0) {
                p += 2;  /* 跳过 chunk 后的 \r\n */
            }
        }

        resp_body = result;
    } else {
        resp_body = strdup(body_start);
    }

    *response = resp_body;
    free(full_response);
    return status_code;
}

/*
 * 进程管理：获取 Clash 进程 PID
 */
pid_t get_clash_pid(void)
{
    /* 使用更精确的匹配：mihomo 二进制路径 */
    char *output = exec_cmd("pgrep -f '/mihomo '");
    if (!output || !*output) {
        free(output);
        return -1;
    }

    pid_t pid = atoi(output);
    free(output);

    /* 验证进程确实存在 */
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "ps -p %d -o comm= 2>/dev/null", pid);
    output = exec_cmd(cmd);
    if (!output || !*output || strstr(output, "mihomo") == NULL) {
        free(output);
        return -1;
    }

    free(output);
    return pid;
}

/*
 * 订阅管理：获取订阅链接
 * 返回: malloc 分配的字符串，需要手动 free
 */
char* get_subscription_url(void)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", getenv("PWD") ? getenv("PWD") : ".", SUBSCRIBE_FILE);

    FILE *fp = fopen(path, "r");
    if (!fp) return NULL;

    static char url[2048];
    if (fgets(url, sizeof(url), fp)) {
        trim(url);
        fclose(fp);
        if (*url) return url;
    }
    fclose(fp);
    return NULL;
}

/*
 * 订阅管理：保存订阅链接
 */
int cmd_set_url(const char *url)
{
    if (!url || !*url) {
        print_err("订阅链接不能为空");
        return -1;
    }

    char path[512];
    snprintf(path, sizeof(path), "%s/%s", getenv("PWD") ? getenv("PWD") : ".", SUBSCRIBE_FILE);

    FILE *fp = fopen(path, "w");
    if (!fp) {
        print_err("无法保存订阅链接");
        return -1;
    }

    fprintf(fp, "%s\n", url);
    fclose(fp);

    printf("订阅链接已保存到：%s\n", path);
    printf("  链接: %s\n", url);
    return 0;
}

/*
 * 订阅管理：显示当前订阅链接
 */
int cmd_show_url(void)
{
    char *url = get_subscription_url();
    if (url && *url) {
        printf("当前订阅链接: %s\n", url);
        return 0;
    } else {
        printf("未设置订阅链接\n");
        printf("  使用 ./clash-ctl set-url <链接> 设置\n");
        return 0;
    }
}


/*
 * 订阅管理：从订阅更新配置
 *
 * 核心思路：让 mihomo 自己处理订阅下载和 base64 解码（mihomo 支持）。
 * 我们只需要生成一个简洁的 proxy.txt，其中包含 proxy-provider type: http。
 */
int cmd_update(void)
{
    char *url = get_subscription_url();
    if (!url || !*url) {
        print_warn("未设置订阅链接");
        printf("  使用 ./clash-ctl set-url <链接> 设置\n");
        return -1;
    }

    printf("正在更新配置文件...\n");

    /* 生成 proxy.txt：使用 proxy-provider type: http
     * mihomo 会自动从 URL 下载内容，并自动检测 base64/URI/YAML 格式 */
    FILE *fp = fopen(CLASH_CONFIG, "w");
    if (!fp) {
        print_err("无法写入配置文件");
        return -1;
    }

    fprintf(fp,
        "# mihomo 配置文件 - 由 clash-ctl 自动生成\n"
        "# 不要手动修改此文件，使用 ./clash-ctl set-url 更新订阅\n"
        "\n"
        "mixed-port: 7890\n"
        "external-controller: 127.0.0.1:9090\n"
        "allow-lan: true\n"
        "mode: rule\n"
        "log-level: info\n"
        "unified-delay: true\n"
        "tcp-concurrent: true\n"
        "discard-blank: true\n"
        "\n"
        "dns:\n"
        "  enable: true\n"
        "  listen: 0.0.0.0:1053\n"
        "  enhanced-mode: fake-ip\n"
        "  fake-ip-range: 198.18.0.1/16\n"
        "  nameserver:\n"
        "    - 223.5.5.5\n"
        "    - 119.29.29.29\n"
        "  fallback:\n"
        "    - tls://8.8.8.8\n"
        "    - tls://1.1.1.1\n"
        "\n"
        "proxy-providers:\n"
        "  sub:\n"
        "    type: http\n"
        "    url: \"%s\"\n"
        "    interval: 3600\n"
        "    path: ./profiles/sub.yaml\n"
        "    health-check:\n"
        "      enable: true\n"
        "      url: http://www.gstatic.com/generate_204\n"
        "      interval: 300\n"
        "    lazy: true\n"
        "\n"
        "proxy-groups:\n"
        "  - name: 自动选择\n"
        "    type: url-test\n"
        "    use:\n"
        "      - sub\n"
        "    url: http://www.gstatic.com/generate_204\n"
        "    interval: 300\n"
        "    tolerance: 50\n"
        "\n"
        "  - name: Manual\n"
        "    type: select\n"
        "    use:\n"
        "      - sub\n"
        "\n"
        "  - name: FINAL\n"
        "    type: select\n"
        "    proxies:\n"
        "      - 自动选择\n"
        "      - Manual\n"
        "      - DIRECT\n"
        "\n"
        "rules:\n"
        "  - MATCH,FINAL\n",
        url);

    fclose(fp);

    print_ok("配置文件已更新（proxy-provider type: http）");
    printf("  配置文件: %s\n", CLASH_CONFIG);
    printf("  订阅 URL: %s\n", url);
    return 0;
}

/*
 * GeoDB 更新：下载 GeoIP 数据库
 */
int cmd_update_geo(void)
{
    printf("正在更新 GeoIP 数据库...\n");

    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
        "curl -sL '%s' -o %s.TMP && mv %s.TMP %s",
        GEO_DB_URL, CLASH_CONFIG, CLASH_CONFIG, CLASH_CONFIG);

    int ret = system(cmd);
    if (ret == 0) {
        print_ok("GeoIP 数据库更新成功");
        return 0;
    } else {
        print_err("GeoIP 数据库更新失败");
        printf("  请检查网络连接\n");
        return -1;
    }
}

/*
 * 进程管理：检查 Clash 是否运行
 */
int is_clash_running(void)
{
    return get_clash_pid() > 0;
}

/*
 * 进程管理：启动 Clash
 */
int cmd_start(void)
{
    if (is_clash_running()) {
        print_warn("Clash 已在运行中");
        return 0;
    }

    /* 自动更新订阅配置 */
    char *url = get_subscription_url();
    if (url && *url) {
        printf("正在更新订阅配置...\n");
        if (cmd_update() != 0) {
            print_warn("订阅更新失败，使用现有配置");
        }
    }

    /* 检查配置文件 */
    if (access(CLASH_CONFIG, R_OK) != 0) {
        print_err("配置文件 " CLASH_CONFIG " 不存在");
        printf("  使用 ./clash-ctl set-url <链接> 设置订阅\n");
        printf("  或手动创建 %s 文件\n", CLASH_CONFIG);
        return -1;
    }

    /* 检查二进制文件 */
    if (access(CLASH_BIN, X_OK) != 0) {
        print_err("找不到可执行文件 " CLASH_BIN);
        return -1;
    }

    /* 后台启动 Clash */
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
        "nohup ./mihomo -d . -f %s > %s 2>&1 &",
        CLASH_CONFIG, CLASH_LOG_FILE);

    int ret = system(cmd);
    if (ret != 0) {
        print_err("启动 Clash 失败");
        return -1;
    }

    /* 等待 Clash 启动 */
    print_info("正在启动 Clash...");
    sleep(2);

    /* 验证启动成功 */
    if (is_clash_running()) {
        print_ok("Clash 启动成功");
        printf("  - HTTP/SOCKS5 代理: 0.0.0.0:7890\n");
        printf("  - 控制面板: http://127.0.0.1:9090\n");
        printf("\n局域网其他设备设置代理:\n");
        printf("  地址: http://<本机IP>:7890\n");
        printf("  类型: HTTP\n");
        return 0;
    } else {
        print_err("启动失败，请检查 " CLASH_LOG_FILE);
        return -1;
    }
}

/*
 * 进程管理：停止 Clash
 */
int cmd_stop(void)
{
    pid_t pid = get_clash_pid();
    if (pid <= 0) {
        print_warn("Clash 未运行");
        return 0;
    }

    if (kill(pid, SIGTERM) == 0) {
        sleep(1);
        print_ok("Clash 已停止");
        return 0;
    } else {
        print_err("停止 Clash 失败");
        return -1;
    }
}

/*
 * 进程管理：重启 Clash
 */
int cmd_restart(void)
{
    cmd_stop();
    sleep(1);
    return cmd_start();
}

/*
 * 状态显示：查看 Clash 运行状态
 */
/* 从 /proxies 获取指定组/proxy 的 now 值 */
static int get_now_value(const char *name, char *out, size_t out_size)
{
    char *json = NULL;
    int code = http_request("GET", "/proxies", NULL, &json);
    if (code != 200 || !json) { free(json); return -1; }

    /* 定位到 "proxies":{ 块，避免匹配到 proxy 列表中的名称 */
    char *proxies_start = strstr(json, "\"proxies\":{");
    if (!proxies_start) { free(json); return -1; }

    char key[300];
    snprintf(key, sizeof(key), "\"%s\":{", name);
    char *g = strstr(proxies_start, key);
    if (!g) { free(json); return -1; }

    char *now = strstr(g, "\"now\"");
    if (!now) { free(json); return -1; }

    char *colon = strchr(now, ':');
    if (!colon) { free(json); return -1; }
    char *q = colon + 1;
    while (*q == ' ' || *q == '"') q++;
    char *end = strchr(q, '"');
    if (!end || (size_t)(end - q) >= out_size) { free(json); return -1; }

    int len = end - q;
    memcpy(out, q, len);
    out[len] = '\0';
    free(json);
    return 0;
}

/* 检查 name 在 /proxies 中是否是组（即 type 不是节点类型） */
static int is_proxy_group(const char *name)
{
    char *json = NULL;
    int code = http_request("GET", "/proxies", NULL, &json);
    if (code != 200 || !json) { free(json); return 0; }

    char key[300];
    snprintf(key, sizeof(key), "\"%s\":{", name);
    char *g = strstr(json, key);
    int is_group = 0;
    if (g) {
        char *type = strstr(g, "\"type\":\"");
        if (type) {
            type += 8;
            char *type_end = strchr(type, '"');
            if (type_end) {
                char t[64];
                int t_len = type_end - type;
                if (t_len < (int)sizeof(t)) {
                    memcpy(t, type, t_len);
                    t[t_len] = '\0';
                    const char *node_types[] = { "Vless", "Vmess", "Shadowsocks", "ShadowsocksR", "Snell", "Http", "Tun", "WireGuard", "Hysteria2", "Tuic", "Compatible" };
                    int nt = sizeof(node_types) / sizeof(node_types[0]);
                    is_group = 1;
                    for (int i = 0; i < nt; i++) {
                        if (strcmp(t, node_types[i]) == 0) { is_group = 0; break; }
                    }
                }
            }
        }
    }
    free(json);
    return is_group;
}

int cmd_status(void)
{
    if (!is_clash_running()) {
        print_warn("Clash 未运行，使用 ./clash-ctl start 启动");
        return -1;
    }

    /* 从 FINAL 开始，逐级获取真实节点名 */
    char current[256];
    if (get_now_value("FINAL", current, sizeof(current)) != 0) {
        print_err("无法获取状态");
        return -1;
    }

    /* 最多递归 5 层，防止死循环 */
    for (int depth = 0; depth < 5; depth++) {
        if (!is_proxy_group(current)) break;
        if (get_now_value(current, current, sizeof(current)) != 0) break;
    }

    printf("\033[1;36m当前节点:\033[0m %s\n", current);
    return 0;
}

int cmd_list(void)
{
    if (!is_clash_running()) {
        print_warn("Clash 未运行");
        return -1;
    }

    char *json = NULL;
    int code = http_request("GET", "/proxies", NULL, &json);
    if (code != 200 || !json) {
        print_err("无法连接到 Clash API");
        free(json);
        return -1;
    }

    /* /proxies 返回 {"proxies": {...}, ...}
     * 顶层每个键是一个 proxy 或 proxy-group
     * 真正的代理节点 type 为 Vless, Vmess, Shadowsocks, Trojan, Snell, Hysteria 等
     * 代理组的 type 为 Proxy, Selector, URLTest, Direct, Reject, Pass, RejectDrop
     * 我们只显示代理节点
     */
    static const char *skip_types[] = {
        "Proxy", "Selector", "URLTest", "Direct",
        "Reject", "Pass", "RejectDrop", "Reject ", NULL
    };

    printf("\n\033[1;33m可用节点列表:\033[0m\n");
    printf("----------------------------------------\n");

    int idx = 1;
    char *p = json;

    while ((p = strstr(p, "\"name\":\"")) != NULL) {
        p += 8;  /* 跳过 "\"name\":\"" */
        char *ne = p;
        while (*ne && *ne != '"') ne++;
        if (!*ne || ne == p) {
            p++;
            continue;
        }

        size_t name_len = ne - p;
        if (name_len >= 256) {
            p++;
            continue;
        }

        char name[256];
        strncpy(name, p, name_len);
        name[name_len] = '\0';

        /* 找这个 key 的 type */
        char *after_name = ne;
        char *type_k = strstr(after_name, "\"type\":\"");
        if (!type_k || (type_k - json) >= (ne - json) + 500) {
            p++;
            continue;
        }
        type_k += 8;  /* 跳过 "\"type\":\"" */
        char *type_end = type_k;
        while (*type_end && *type_end != '"') type_end++;
        size_t type_len = type_end - type_k;
        if (type_len >= 64) {
            p++;
            continue;
        }

        char type[64];
        strncpy(type, type_k, type_len);
        type[type_len] = '\0';

        /* 检查是否应该跳过 */
        int skip = 0;
        for (int i = 0; skip_types[i]; i++) {
            if (strcmp(type, skip_types[i]) == 0) {
                skip = 1;
                break;
            }
        }

        if (!skip && name_len > 0) {
            printf("  %2d. %s\n", idx++, name);
        }

        p = ne + 1;
    }

    printf("----------------------------------------\n");
    free(json);

    cmd_status();
    return 0;
}

/*
 * URL 编码（简化版，仅处理中文和空格）
 */
void url_encode(const char *src, char *dst, size_t dst_size)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t j = 0;

    while (*src && j < dst_size - 4) {
        unsigned char c = *src;
        if (c >= 0x80) {
            /* UTF-8 中文字符 */
            if (j < dst_size - 4) {
                dst[j++] = '%';
                dst[j++] = hex[(c >> 4) & 0x0F];
                dst[j++] = hex[c & 0x0F];
            }
        } else if (c == ' ' || c == '#' || c == '%') {
            dst[j++] = '%';
            dst[j++] = hex[(c >> 4) & 0x0F];
            dst[j++] = hex[c & 0x0F];
        } else {
            dst[j++] = c;
        }
        src++;
    }
    dst[j] = '\0';
}

/*
 * 节点选择：切换到指定节点
 */
/* 根据索引查找节点名 */
static int get_node_name_by_index(int index, char *out_name, size_t out_size)
{
    char *json = NULL;
    int code = http_request("GET", "/proxies", NULL, &json);
    if (code != 200 || !json) {
        free(json);
        return -1;
    }

    /* 提取所有节点名称和类型 */
    typedef struct { char name[256]; char type[64]; } node_t;
    node_t nodes[256];
    int count = 0;

    const char *p = json;
    const char *skip[] = { "Proxy", "Selector", "URLTest", "Direct", "Reject", "Pass", "RejectDrop" };
    int skip_n = sizeof(skip) / sizeof(skip[0]);

    while (count < 256) {
        const char *name_tag = strstr(p, "\"name\":\"");
        if (!name_tag) break;
        name_tag += 8;
        const char *name_end = strchr(name_tag, '"');
        if (!name_end || (size_t)(name_end - name_tag) >= sizeof(nodes[count].name)) { p = name_tag; continue; }
        int name_len = name_end - name_tag;
        memcpy(nodes[count].name, name_tag, name_len);
        nodes[count].name[name_len] = '\0';

        const char *type_tag = strstr(name_end, "\"type\":\"");
        if (!type_tag) { p = name_end; continue; }
        type_tag += 8;
        const char *type_end = strchr(type_tag, '"');
        if (!type_end || (size_t)(type_end - type_tag) >= sizeof(nodes[count].type)) { p = name_end; continue; }
        int type_len = type_end - type_tag;
        memcpy(nodes[count].type, type_tag, type_len);
        nodes[count].type[type_len] = '\0';

        int is_group = 0;
        for (int i = 0; i < skip_n; i++) {
            if (strcmp(nodes[count].type, skip[i]) == 0) { is_group = 1; break; }
        }
        if (!is_group) count++;
        p = type_end;
    }

    free(json);

    if (index < 1 || index > count) return -1;
    strncpy(out_name, nodes[index - 1].name, out_size - 1);
    out_name[out_size - 1] = '\0';
    return 0;
}

int cmd_select(const char *node_name)
{
    if (!is_clash_running()) {
        print_warn("Clash 未运行");
        return -1;
    }

    if (!node_name || !*node_name) {
        print_err("请指定节点名称");
        return -1;
    }

    char selected[256];
    char path[256];
    char body[512];

    /* 如果是数字，则作为索引解析 */
    if (isdigit(node_name[0])) {
        int idx = atoi(node_name);
        if (get_node_name_by_index(idx, selected, sizeof(selected)) == 0) {
            printf("  索引 %d -> 切换到 [%s]\n", idx, selected);
            node_name = selected;
        } else {
            print_err("索引超出范围");
            return -1;
        }
    }

    /* 切换 proxy-provider "sub" 的节点 */
    snprintf(path, sizeof(path), "/providers/proxies/sub/select");
    snprintf(body, sizeof(body), "{\"name\": \"%s\"}", node_name);
    char *response = NULL;
    int code = http_request("PUT", path, body, &response);
    free(response);

    if (code == 204 || code == 200) {
        print_ok("节点切换成功");
        sleep(1);
        cmd_status();
        return 0;
    }

    /* fallback: 通过 /proxies/Manual 切换 */
    snprintf(path, sizeof(path), "/proxies/Manual");
    code = http_request("PUT", path, body, &response);
    free(response);

    if (code == 204 || code == 200) {
        print_ok("节点切换成功");
        sleep(1);
        cmd_status();
        return 0;
    }

    /* fallback: 通过 /proxies/FINAL 切换 */
    snprintf(path, sizeof(path), "/proxies/FINAL");
    code = http_request("PUT", path, body, &response);
    free(response);

    if (code == 204 || code == 200) {
        print_ok("节点切换成功");
        sleep(1);
        cmd_status();
        return 0;
    }

    print_err("节点切换失败");
    printf("  (HTTP %d)\n", code);
    return -1;
}

/*
 * 帮助信息
 */
void print_usage(const char *prog)
{
    printf("\n\033[1mClash 控制工具\033[0m - 纯 POSIX 实现，无需外部依赖\n");
    printf("\n用法: %s <命令> [参数]\n\n", prog);
    printf("\033[1m服务控制:\033[0m\n");
    printf("  start              启动 Clash 代理服务\n");
    printf("  stop               停止 Clash 代理服务\n");
    printf("  restart            重启 Clash 代理服务\n");
    printf("\n\033[1m状态查询:\033[0m\n");
    printf("  status             查看当前状态和节点\n");
    printf("  list               列出所有可用节点\n");
    printf("\n\033[1m节点管理:\033[0m\n");
    printf("  select <节点名>     切换到指定节点\n");
    printf("\n\033[1m订阅管理:\033[0m\n");
    printf("  set-url <链接>      设置订阅链接\n");
    printf("  show-url           显示当前订阅链接\n");
    printf("  update             从订阅更新配置\n");
    printf("\n\033[1m其他:\033[0m\n");
    printf("  update-geo         更新 GeoIP 数据库\n");
    printf("  help               显示此帮助信息\n");
    printf("\n\033[1m示例:\033[0m\n");
    printf("  %s set-url https://example.com/sub\n", prog);
    printf("  %s start\n", prog);
    printf("  %s list\n", prog);
    printf("  %s select '新加坡 01'\n", prog);
    printf("\n\033[1m配置:\033[0m\n");
    printf("  配置文件: %s\n", CLASH_CONFIG);
    printf("  订阅链接: ~/%s\n", SUBSCRIBE_FILE);
    printf("  代理端口: 7890 (HTTP/SOCKS5 混合)\n");
    printf("  API 端口: %d\n", CLASH_API_PORT);
    printf("\n\033[1m局域网使用:\033[0m\n");
    printf("  在其他设备的系统代理设置中填入:\n");
    printf("    地址: http://<本机IP>:7890\n");
    printf("    类型: HTTP\n");
    printf("\n");
}

/* ====== 主程序入口 ====== */
int main(int argc, char *argv[])
{
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    int ret = 0;

    if (strcmp(argv[1], "start") == 0) {
        ret = cmd_start();
    }
    else if (strcmp(argv[1], "stop") == 0) {
        ret = cmd_stop();
    }
    else if (strcmp(argv[1], "restart") == 0) {
        ret = cmd_restart();
    }
    else if (strcmp(argv[1], "status") == 0) {
        ret = cmd_status();
    }
    else if (strcmp(argv[1], "list") == 0) {
        ret = cmd_list();
    }
    else if (strcmp(argv[1], "select") == 0) {
        if (argc < 3) {
            print_err("请指定节点名称");
            ret = -1;
        } else {
            ret = cmd_select(argv[2]);
        }
    }
    else if (strcmp(argv[1], "set-url") == 0) {
        if (argc < 3) {
            print_err("请指定订阅链接");
            ret = -1;
        } else {
            ret = cmd_set_url(argv[2]);
        }
    }
    else if (strcmp(argv[1], "show-url") == 0) {
        ret = cmd_show_url();
    }
    else if (strcmp(argv[1], "update") == 0) {
        ret = cmd_update();
    }
    else if (strcmp(argv[1], "update-geo") == 0) {
        ret = cmd_update_geo();
    }
    else if (strcmp(argv[1], "help") == 0 || strcmp(argv[1], "--help") == 0 ||
             strcmp(argv[1], "-h") == 0) {
        print_usage(argv[0]);
    }
    else {
        print_err("未知命令");
        print_usage(argv[0]);
        ret = 1;
    }

    return ret;
}
