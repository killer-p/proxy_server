#!/bin/bash
#
# test_run.sh - 自动化测试脚本
#
# 用法: ./test_run.sh <订阅链接>
#
# 流程（模拟用户操作）:
#   cd <项目根目录>
#   make
#   cd bin
#   ./clash-ctl set-url <链接>
#   ./clash-ctl start
#   curl 测试 Google 连通性
#   ./clash-ctl stop
#
# 每一步骤独立记录日志到 test/logs/ 目录
#

set -e

# ====== 路径配置 ======
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
LOG_DIR="${SCRIPT_DIR}/logs"
TIMESTAMP=$(date '+%Y%m%d_%H%M%S')

# ====== 日志函数 ======
log() {
    local step="$1"
    local msg="$2"
    local ts=$(date '+%Y-%m-%d %H:%M:%S')
    echo "[${ts}] [${step}] ${msg}"
}

log_file() {
    local step="$1"
    local msg="$2"
    local ts=$(date '+%Y-%m-%d %H:%M:%S')
    echo "[${ts}] [${step}] ${msg}" >> "${LOG_DIR}/${step}.log"
}

# ====== 订阅链接 ======
# 用法（三选一）：
#   1. 编辑此变量后直接执行:  SUB_URL="..."
#   2. 命令行传入:           ./test_run.sh <订阅链接>
#   3. 环境变量传入:         SUB_URL=... ./test_run.sh
# 优先级：命令行参数 > 环境变量 > 此处变量（空白）

# 仅在未设置时赋予默认值（避免覆盖环境变量 SUB_URL）
: "${SUB_URL:=}"

# ====== 参数检查 ======
if [ -z "$SUB_URL" ] && [ $# -lt 1 ]; then
    echo "用法: ./test_run.sh <订阅链接>"
    echo "  或在脚本顶部修改 SUB_URL 变量后直接执行"
    echo "示例: ./test_run.sh https://example.com/sub"
    exit 1
fi

# 命令行参数优先
if [ $# -ge 1 ]; then
    SUB_URL="$1"
fi

# ====== 初始化 ======
mkdir -p "$LOG_DIR"
log "INIT" "测试开始，订阅链接: ${SUB_URL}"
log_file "INIT" "测试开始，订阅链接: ${SUB_URL}"

# ====== 步骤 1: make ======
step_make() {
    log "MAKE" "编译项目..."
    log_file "MAKE" "编译项目..."

    (
        cd "$PROJECT_DIR"
        if make > >(tee -a "${LOG_DIR}/MAKE.log") 2>&1; then
            log "MAKE" "编译成功"
            log_file "MAKE" "编译成功"
        else
            log "MAKE" "编译失败"
            log_file "MAKE" "编译失败"
            exit 1
        fi
    )
}

# ====== 步骤 2: set-url ======
step_set_url() {
    log "SET-URL" "保存订阅链接..."
    log_file "SET-URL" "保存订阅链接..."

    (
        cd "$PROJECT_DIR/bin"
        if ./clash-ctl set-url "$SUB_URL" > >(tee -a "${LOG_DIR}/SET-URL.log") 2>&1; then
            log "SET-URL" "订阅链接保存成功"
            log_file "SET-URL" "订阅链接保存成功"
        else
            log "SET-URL" "订阅链接保存失败"
            log_file "SET-URL" "订阅链接保存失败"
            exit 1
        fi
    )
}

# ====== 步骤 3: start ======
step_start() {
    log "START" "启动代理服务..."
    log_file "START" "启动代理服务..."

    (
        cd "$PROJECT_DIR/bin"
        if ./clash-ctl start > >(tee -a "${LOG_DIR}/START.log") 2>&1; then
            log "START" "启动命令执行完成"
            log_file "START" "启动命令执行完成"
        else
            log "START" "启动命令执行失败"
            log_file "START" "启动命令执行失败"
            exit 1
        fi
    )
}

# ====== 步骤 4: wait ready ======
step_wait() {
    log "WAIT" "等待服务就绪（10秒）..."
    log_file "WAIT" "等待服务就绪（10秒）..."
    sleep 10
    log "WAIT" "等待完成"
    log_file "WAIT" "等待完成"
}

# ====== 步骤 5: google-test ======
step_google_test() {
    log "GOOGLE-TEST" "测试 Google 连通性（通过代理 127.0.0.1:7890）..."
    log_file "GOOGLE-TEST" "测试 Google 连通性（通过代理 127.0.0.1:7890）..."

    local http_code
    local start_time=$(date +%s%3N)

    http_code=$(curl -s -o /dev/null -w "%{http_code}" \
        --proxy http://127.0.0.1:7890 \
        --connect-timeout 15 \
        --max-time 30 \
        "https://www.google.com/generate_204" 2>>"${LOG_DIR}/GOOGLE-TEST.log")

    local end_time=$(date +%s%3N)
    local elapsed=$((end_time - start_time))

    log "GOOGLE-TEST" "HTTP 状态码: ${http_code}, 耗时: ${elapsed}ms"
    log_file "GOOGLE-TEST" "HTTP 状态码: ${http_code}, 耗时: ${elapsed}ms"

    if [ "$http_code" = "204" ]; then
        log "GOOGLE-TEST" "Google 访问成功 ✅"
        log_file "GOOGLE-TEST" "Google 访问成功"
    else
        log "GOOGLE-TEST" "Google 访问失败（HTTP ${http_code}）❌"
        log_file "GOOGLE-TEST" "Google 访问失败（HTTP ${http_code}）"
        return 1
    fi
}

# ====== 步骤 6: stop ======
step_stop() {
    log "STOP" "停止代理服务..."
    log_file "STOP" "停止代理服务..."

    (
        cd "$PROJECT_DIR/bin"
        if ./clash-ctl stop > >(tee -a "${LOG_DIR}/STOP.log") 2>&1; then
            log "STOP" "停止命令执行完成"
            log_file "STOP" "停止命令执行完成"
        else
            log "STOP" "停止命令执行失败"
            log_file "STOP" "停止命令执行失败"
            exit 1
        fi
    )
}

# ====== 主流程 ======
FAILED=0

log "MAIN" "========== 测试流程开始 =========="
log_file "MAIN" "========== 测试流程开始 =========="

step_make    || FAILED=1
step_set_url || FAILED=1
step_start   || FAILED=1
step_wait
step_google_test || FAILED=1
step_stop    || FAILED=1

log "MAIN" "========== 测试流程结束 =========="
log_file "MAIN" "========== 测试流程结束 =========="

# 生成汇总报告
REPORT="${LOG_DIR}/report_${TIMESTAMP}.log"
{
    echo "========== 测试报告 =========="
    echo "时间: $(date '+%Y-%m-%d %H:%M:%S')"
    echo "订阅: ${SUB_URL}"
    echo "结果: $([ $FAILED -eq 0 ] && echo '✅ 正常' || echo '❌ 异常')"
    echo "日志目录: ${LOG_DIR}"
    echo "============================"
} | tee "$REPORT"

exit $FAILED
