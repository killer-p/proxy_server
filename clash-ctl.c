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
    char *output = exec_cmd("pgrep -f 'mihomo.*proxy.txt'");
    if (!output || !*output) {
        free(output);
        return -1;
    }

    pid_t pid = atoi(output);
    free(output);
    return pid;
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

    /* 检查配置文件 */
    if (access(CLASH_CONFIG, R_OK) != 0) {
        print_err("配置文件 " CLASH_CONFIG " 不存在");
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
int cmd_status(void)
{
    if (!is_clash_running()) {
        print_warn("Clash 未运行，使用 ./clash-ctl start 启动");
        return -1;
    }

    char *json = NULL;
    int code = http_request("GET", "/proxies", NULL, &json);

    if (code != 200 || !json) {
        print_err("无法连接到 Clash API");
        free(json);
        return -1;
    }

    /* 查找 "永雏塔菲的魔法卷轴" 对象（键: { 的形式） */
    char *key_pattern = "\"永雏塔菲的魔法卷轴\":{";
    char *group = strstr(json, key_pattern);
    if (group) {
        /* 跳过键名部分，定位到对象的开始 */
        char *obj_start = group + strlen(key_pattern) - 1;  /* 指向 { */
        char *obj_end = NULL;

        /* 查找对应的结束括号 */
        int depth = 0;
        for (char *p = obj_start; *p; p++) {
            if (*p == '{') {
                depth++;
            } else if (*p == '}') {
                depth--;
                if (depth == 0) {
                    obj_end = p;
                    break;
                }
            }
        }

        if (obj_end) {
            /* 在对象内查找 "now" 字段 */
            char saved_end = *obj_end;
            *obj_end = '\0';

            char *now = strstr(obj_start, "\"now\"");
            if (now) {
                char *colon = strchr(now, ':');
                if (colon) {
                    char *quote = strchr(colon + 1, '"');
                    if (quote) {
                        quote++;
                        char *end = strchr(quote, '"');
                        if (end) {
                            *end = '\0';
                            printf("\033[1;36m当前节点:\033[0m %s\n", quote);
                            *obj_end = saved_end;
                            free(json);
                            return 0;
                        }
                    }
                }
            }
            *obj_end = saved_end;
        }
    }

    print_info("状态: 运行中");
    free(json);
    return 0;
}

/*
 * 节点列表：显示所有可用节点
 */
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

    /* 查找 "all" 数组（在组对象内） */
    char *group = strstr(json, "\"永雏塔菲的魔法卷轴\"");
    if (!group) {
        printf("未找到代理组\n");
        free(json);
        return -1;
    }

    /* 查找 "all" 数组 */
    char *all = strstr(group, "\"all\"");
    if (!all) {
        free(json);
        return -1;
    }

    char *array_start = strchr(all, '[');
    if (!array_start) {
        free(json);
        return -1;
    }

    /* 找到对应的结束括号 */
    int depth = 0;
    char *p = array_start;
    while (*p) {
        if (*p == '[') depth++;
        else if (*p == ']') {
            depth--;
            if (depth == 0) break;
        }
        p++;
    }

    if (depth != 0) {
        free(json);
        return -1;
    }

    *p = '\0';  /* 临时截断字符串 */

    printf("\n\033[1;33m可用节点列表:\033[0m\n");
    printf("----------------------------------------\n");

    /* 逐个提取节点名称 */
    char *ptr = array_start + 1;
    int idx = 1;
    while (*ptr) {
        /* 跳过空白和逗号 */
        while (*ptr && (*ptr == ' ' || *ptr == ',' || *ptr == '\n' || *ptr == '\t')) ptr++;
        if (!*ptr || *ptr == ']') break;

        if (*ptr == '"') {
            ptr++;
            char *name_end = strchr(ptr, '"');
            if (!name_end) break;

            *name_end = '\0';
            printf("  %2d. %s\n", idx++, ptr);
            ptr = name_end + 1;
        } else {
            ptr++;
        }
    }

    printf("----------------------------------------\n");
    *p = ']';  /* 恢复字符串 */

    free(json);

    /* 显示当前节点 */
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

    /* 目标代理组名称 */
    const char *group_name = "永雏塔菲的魔法卷轴";

    /* URL 编码代理组名称 */
    char encoded_group[256];
    url_encode(group_name, encoded_group, sizeof(encoded_group));

    /* 构造 API 路径 */
    char path[512];
    snprintf(path, sizeof(path), "/proxies/%s", encoded_group);

    /* 构造请求体 */
    char body[512];
    snprintf(body, sizeof(body), "{\"name\": \"%s\"}", node_name);

    /* 发送 PUT 请求 */
    char *response = NULL;
    int code = http_request("PUT", path, body, &response);

    free(response);

    if (code == 204 || code == 200) {
        print_ok("节点切换成功");
        sleep(1);
        cmd_status();
        return 0;
    } else {
        print_err("节点切换失败");
        printf("  (HTTP %d)\n", code);
        return -1;
    }
}

/*
 * 帮助信息
 */
void print_usage(const char *prog)
{
    printf("\n\033[1mClash 控制工具\033[0m - 纯 POSIX 实现，无需外部依赖\n");
    printf("\n用法: %s <命令> [参数]\n\n", prog);
    printf("\033[1m命令:\033[0m\n");
    printf("  start              启动 Clash 代理服务\n");
    printf("  stop               停止 Clash 代理服务\n");
    printf("  restart            重启 Clash 代理服务\n");
    printf("  status             查看当前状态\n");
    printf("  list               列出所有可用节点\n");
    printf("  select <节点名>     切换到指定节点\n");
    printf("  help               显示此帮助信息\n");
    printf("\n\033[1m示例:\033[0m\n");
    printf("  %s start\n", prog);
    printf("  %s list\n", prog);
    printf("  %s select '新加坡 01'\n", prog);
    printf("\n\033[1m配置:\033[0m\n");
    printf("  配置文件: %s\n", CLASH_CONFIG);
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
