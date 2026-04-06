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
#include <sys/time.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <time.h>
#include <ctype.h>

#include "lib/cjson/cJSON.h"

/* ====== 配置常量 ====== */
#define CLASH_API_HOST   "127.0.0.1"
#define CLASH_API_PORT   9090
#define CLASH_SUB_YAML "profiles/sub.yaml"
#define CLASH_CONFIG     "proxy.txt"
#define CLASH_BIN        "mihomo"
#define CLASH_LOG_FILE   "clash.log"
#define SUBSCRIBE_FILE   ".clash-url"
#define GEO_DB_URL       "https://github.com/Loyalsoldier/geoip/releases/download/latest/Country.mmdb"
#define LOG_CHECK_LINES  100   // 启动后扫描日志末尾行数

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
    if (!fp) {
        printf("无法找到订阅链接：%s\n", path);
        return NULL;
    }

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
        "    path: profiles/sub.yaml\n"
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

    // 删除clash的订阅缓存
    remove(CLASH_SUB_YAML);
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
 * 日志检查：读取日志文件末尾 N 行
 * 通过 exec_cmd 调用 tail 命令，tail 是 GNU coreutils 内置的，无需额外依赖
 * 返回: malloc 分配的字符串，需要手动 free
 */
static char* read_log_tail(const char *log_path, int n)
{
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "tail -n %d '%s' 2>/dev/null", n, log_path);
    return exec_cmd(cmd);
}

/*
 * 日志检查：扫描日志内容中的 error 和 warning 行
 * log_content: 日志文本（多行）
 * err_lines: 输出数组，每项最多 511 字符
 * max: err_lines 最大条目数
 * 返回: 错误/warning 行数量
 */
static int scan_log_errors(const char *log_content, char err_lines[][512], int max)
{
    int count = 0;
    if (!log_content) return 0;

    const char *p = log_content;
    while (*p && count < max) {
        const char *err  = strstr(p, "level=error");
        const char *warn = strstr(p, "level=warning");
        const char *match = NULL;
        if (err && (!warn || err < warn)) match = err;
        else if (warn) match = warn;

        if (!match) break;

        const char *line_start = match;
        const char *line_end = strchr(match, '\n');
        if (!line_end) line_end = line_start + strlen(line_start);

        int len = line_end - line_start;
        if (len > 511) len = 511;
        strncpy(err_lines[count], line_start, len);
        err_lines[count][len] = '\0';
        count++;
        p = line_end;
    }
    return count;
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
    printf("start cmd %s success\n", cmd);

    /* 等待 Clash 启动 */
    print_info("正在启动 Clash...");
    sleep(3);

    /* ---- 日志错误扫描 ---- */
    char *log_tail = read_log_tail(CLASH_LOG_FILE, LOG_CHECK_LINES);
    int err_count = 0;
    char err_lines[20][512];
    if (log_tail && *log_tail) {
        err_count = scan_log_errors(log_tail, err_lines, 20);
    }
    free(log_tail);

    /* ---- API 健康检查 ---- */
    char *api_json = NULL;
    int api_code = http_request("GET", "/proxies", NULL, &api_json);
    int api_ok = (api_code == 200 && api_json != NULL);
    free(api_json);

    /* ---- Provider 节点检查 ---- */
    int provider_ok = 0;
    if (api_ok) {
        char *prov_json = NULL;
        int prov_code = http_request("GET", "/providers/proxies/sub", NULL, &prov_json);
        if (prov_code == 200 && prov_json) {
            cJSON *root = cJSON_Parse(prov_json);
            if (root) {
                cJSON *proxies = cJSON_GetObjectItem(root, "proxies");
                if (cJSON_IsArray(proxies) && cJSON_GetArraySize(proxies) > 0) {
                    provider_ok = 1;
                }
                cJSON_Delete(root);
            }
        }
        free(prov_json);
    }

    /* 验证启动成功 */
    if (api_ok && provider_ok) {
        if (err_count > 0) {
            print_warn("mihomo 已启动但存在以下问题：");
            for (int i = 0; i < err_count; i++) {
                printf("  %s\n", err_lines[i]);
            }
            printf("请查看 %s 了解详情\n", CLASH_LOG_FILE);
        }
        print_ok("mihomo 启动成功");
        printf("  - HTTP/SOCKS5 代理: 0.0.0.0:7890\n");
        printf("  - 控制面板: http://127.0.0.1:9090\n");
        printf("\n局域网其他设备设置代理:\n");
        printf("  地址: http://<本机IP>:7890\n");
        printf("  类型: HTTP\n");
        return 0;
    } else if (is_clash_running()) {
        /* 进程存在但初始化失败，清理残留进程 */
        pid_t pid = get_clash_pid();
        if (pid > 0) kill(pid, SIGTERM);
        print_err("mihomo 启动失败");
        if (!provider_ok) {
            print_err("订阅配置无效或获取失败，请检查订阅链接是否正确");
            printf("  订阅链接: %s\n", url ? url : "(未设置)");
        }
        if (err_count > 0) {
            printf("  日志中的错误：\n");
            for (int i = 0; i < err_count; i++) {
                printf("  %s\n", err_lines[i]);
            }
        }
        printf("  请查看 %s 了解详情\n", CLASH_LOG_FILE);
        return -1;
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

    /* 优先通过 REST API 优雅退出 */
    char *resp = NULL;
    int code = http_request("PUT", "/quit", NULL, &resp);
    free(resp);

    if (code == 200) {
        sleep(1);
        print_ok("mihomo 已关闭");
        return 0;
    }

    /* fallback: SIGTERM */
    if (kill(pid, SIGTERM) == 0) {
        sleep(1);
        print_ok("Clash 已停止");
        return 0;
    }

    print_err("停止 Clash 失败");
    return -1;
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
 * 工具函数：流量格式化
 * bytes: 字节数，输出 Human-readable 字符串
 */
static void format_bytes(long long bytes, char *out, size_t size)
{
    if (bytes < 0)      { snprintf(out, size, "N/A"); return; }
    if (bytes < 1024)                { snprintf(out, size, "%lld B", bytes); return; }
    if (bytes < 1024*1024)           { snprintf(out, size, "%.1f KB", bytes/1024.0); return; }
    if (bytes < 1024*1024*1024)     { snprintf(out, size, "%.2f MB", bytes/(1024.0*1024)); return; }
    snprintf(out, size, "%.02f GB", bytes/(1024.0*1024*1024));
}

/*
 * 流量监控：实时显示累计总流量 + 瞬时速率
 * 用户按 Ctrl+C 退出
 */
static void traffic_monitor(void)
{
    struct sockaddr_in server;
    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons(api_port);
    inet_pton(AF_INET, CLASH_API_HOST, &server.sin_addr);

    int tsock = socket(AF_INET, SOCK_STREAM, 0);
    if (tsock < 0) return;

    /* SIGINT 由系统处理（Ctrl+C 自动终止进程），此处不拦截 */
    if (connect(tsock, (struct sockaddr *)&server, sizeof(server)) != 0) {
        close(tsock);
        return;
    }

    const char *req = "GET /traffic HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: keep-alive\r\n\r\n";
    send(tsock, req, strlen(req), 0);

    printf("\n  \033[1;33m按 Ctrl+C 退出\033[0m\n");
    printf("  %-12s %-12s   %-12s %s\n", "↓实时", "↑实时", "↓累计", "↑累计");
    printf("  --------------------------------------------------\n");

    char recv_buf[1024];
    char line_buf[1024];
    int line_len = 0;
    long long total_up = 0, total_down = 0;

    while (1) {
        fd_set rset;
        FD_ZERO(&rset);
        FD_SET(tsock, &rset);
        struct timeval tv = {1, 0};
        int sr = select(tsock + 1, &rset, NULL, NULL, &tv);

        if (sr < 0) break;
        if (sr == 0) continue;  /* 超时，继续等待 */

        int r = recv(tsock, recv_buf, sizeof(recv_buf) - 1, 0);
        if (r <= 0) break;  /* 连接断开或 Ctrl+C */

        for (int i = 0; i < r; i++) {
            if (recv_buf[i] == '\n') {
                line_buf[line_len] = '\0';
                if (line_len > 0) {
                    cJSON *root = cJSON_Parse(line_buf);
                    if (root) {
                        cJSON *ut = cJSON_GetObjectItem(root, "upTotal");
                        cJSON *dt = cJSON_GetObjectItem(root, "downTotal");
                        cJSON *u  = cJSON_GetObjectItem(root, "up");
                        cJSON *d  = cJSON_GetObjectItem(root, "down");
                        if (cJSON_IsNumber(ut)) total_up   = (long long)ut->valuedouble;
                        if (cJSON_IsNumber(dt)) total_down = (long long)dt->valuedouble;

                        char up_total[16], down_total[16], up_rate[16], down_rate[16];
                        long long ur = cJSON_IsNumber(u) ? (long long)u->valuedouble : 0;
                        long long dr = cJSON_IsNumber(d) ? (long long)d->valuedouble : 0;
                        format_bytes(total_up,   up_total,   sizeof(up_total));
                        format_bytes(total_down, down_total, sizeof(down_total));
                        format_bytes(ur,        up_rate,    sizeof(up_rate));
                        format_bytes(dr,        down_rate,  sizeof(down_rate));

                        printf("  \033[1;36m↓ %s\033[0m  \033[1;36m↑ %s\033[0m  (已用 ↓ %s  ↑ %s)\n",
                               down_rate[0]=='N' ? "0 B" : down_rate,
                               up_rate[0]=='N' ? "0 B" : up_rate,
                               down_total, up_total);
                        fflush(stdout);
                        cJSON_Delete(root);
                    }
                }
                line_len = 0;
            } else if (line_len < (int)sizeof(line_buf) - 1) {
                line_buf[line_len++] = recv_buf[i];
            }
        }
    }

    close(tsock);
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

    cJSON *root = cJSON_Parse(json);
    free(json);
    if (!root) return -1;

    cJSON *proxies = cJSON_GetObjectItem(root, "proxies");
    if (!cJSON_IsObject(proxies)) { cJSON_Delete(root); return -1; }

    cJSON *item = cJSON_GetObjectItem(proxies, name);
    if (!item) { cJSON_Delete(root); return -1; }

    cJSON *now = cJSON_GetObjectItem(item, "now");
    if (!cJSON_IsString(now) || !now->valuestring) {
        cJSON_Delete(root);
        return -1;
    }

    strncpy(out, now->valuestring, out_size - 1);
    out[out_size - 1] = '\0';
    cJSON_Delete(root);
    return 0;
}

/* 检查 name 在 /proxies 中是否是组（type 不是节点类型） */
static int is_proxy_group(const char *name)
{
    static const char *node_types[] = {
        "Vless", "Vmess", "Shadowsocks", "ShadowsocksR",
        "Snell", "Http", "Tun", "WireGuard", "Hysteria2", "Tuic", "Compatible"
    };
    int nt = sizeof(node_types) / sizeof(node_types[0]);

    char *json = NULL;
    int code = http_request("GET", "/proxies", NULL, &json);
    if (code != 200 || !json) { free(json); return 0; }

    cJSON *root = cJSON_Parse(json);
    free(json);
    if (!root) return 0;

    cJSON *proxies = cJSON_GetObjectItem(root, "proxies");
    if (!cJSON_IsObject(proxies)) { cJSON_Delete(root); return 0; }

    cJSON *item = cJSON_GetObjectItem(proxies, name);
    int is_group = 1;
    if (item) {
        cJSON *type = cJSON_GetObjectItem(item, "type");
        if (cJSON_IsString(type)) {
            for (int i = 0; i < nt; i++) {
                if (strcmp(type->valuestring, node_types[i]) == 0) {
                    is_group = 0;
                    break;
                }
            }
        }
    }
    cJSON_Delete(root);
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
    traffic_monitor();
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

    cJSON *root = cJSON_Parse(json);
    free(json);
    if (!root) {
        print_err("API 响应解析失败");
        return -1;
    }

    static const char *skip_types[] = {
        "Proxy", "Selector", "URLTest", "Direct",
        "Reject", "Pass", "RejectDrop", NULL
    };

    cJSON *proxies = cJSON_GetObjectItem(root, "proxies");
    if (!cJSON_IsObject(proxies)) {
        cJSON_Delete(root);
        print_err("API 响应格式错误");
        return -1;
    }

    printf("\n\033[1;33m可用节点列表:\033[0m\n");
    printf("----------------------------------------\n");

    int idx = 1;
    for (cJSON *item = proxies->child; item != NULL; item = item->next) {
        if (!cJSON_IsObject(item)) continue;

        cJSON *type = cJSON_GetObjectItem(item, "type");
        if (!cJSON_IsString(type)) continue;

        int skip = 0;
        for (int i = 0; skip_types[i]; i++) {
            if (strcmp(type->valuestring, skip_types[i]) == 0) {
                skip = 1;
                break;
            }
        }
        if (skip) continue;

        printf("  %2d. %s\n", idx++, item->string);
    }

    printf("----------------------------------------\n");
    cJSON_Delete(root);

    cmd_status();
    return 0;
}

static int get_node_name_by_index(int index, char *out_name, size_t out_size)
{
    char *json = NULL;
    int code = http_request("GET", "/proxies", NULL, &json);
    if (code != 200 || !json) { free(json); return -1; }

    cJSON *root = cJSON_Parse(json);
    free(json);
    if (!root) return -1;

    cJSON *proxies = cJSON_GetObjectItem(root, "proxies");
    if (!cJSON_IsObject(proxies)) { cJSON_Delete(root); return -1; }

    static const char *skip_types[] = {
        "Proxy", "Selector", "URLTest", "Direct", "Reject", "Pass", "RejectDrop", NULL
    };

    int idx = 1;
    for (cJSON *item = proxies->child; item != NULL; item = item->next) {
        if (!cJSON_IsObject(item)) continue;

        cJSON *type = cJSON_GetObjectItem(item, "type");
        if (!cJSON_IsString(type)) continue;

        int skip = 0;
        for (int i = 0; skip_types[i]; i++) {
            if (strcmp(type->valuestring, skip_types[i]) == 0) {
                skip = 1;
                break;
            }
        }
        if (skip) continue;

        if (idx == index) {
            strncpy(out_name, item->string, out_size - 1);
            out_name[out_size - 1] = '\0';
            cJSON_Delete(root);
            return 0;
        }
        idx++;
    }

    cJSON_Delete(root);
    return -1;
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
