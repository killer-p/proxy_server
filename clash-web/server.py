#!/usr/bin/env python3
"""
clash-web - mihomo 代理管理 Web 界面
纯 Python3 内置库，无外部依赖
"""

import sys
import os
import json
import subprocess
import traceback
from http.server import HTTPServer, SimpleHTTPRequestHandler
from urllib.parse import urlparse

# ============ 配置 ============
PORT = 8080
API_HOST = "127.0.0.1"
API_PORT = 9090
CLASHCTL_BIN = os.path.join(os.path.dirname(__file__), "../bin/clash-ctl")
SUBSCRIBE_FILE = os.path.join(os.path.dirname(__file__), "../bin/.clash-url")

# ============ 工具函数 ============

def api_get(path):
    """转发 GET 请求到 mihomo REST API"""
    try:
        import http.client
        conn = http.client.HTTPConnection(API_HOST, API_PORT, timeout=5)
        conn.request("GET", path, headers={"Connection": "close"})
        resp = conn.getresponse()
        body = resp.read()
        conn.close()
        return resp.status, body.decode("utf-8", errors="replace")
    except Exception as e:
        return 502, json.dumps({"error": str(e)})


def api_put(path, body=""):
    """转发 PUT 请求到 mihomo REST API"""
    try:
        import http.client
        conn = http.client.HTTPConnection(API_HOST, API_PORT, timeout=5)
        headers = {"Content-Type": "application/json", "Connection": "close"}
        conn.request("PUT", path, body=body.encode(), headers=headers)
        resp = conn.getresponse()
        resp.read()
        conn.close()
        return resp.status
    except Exception as e:
        return 502


def clash_running():
    """检查 mihomo 是否在运行"""
    try:
        subprocess.run(["pgrep", "-f", "/mihomo "],
                       capture_output=True, check=True)
        return True
    except subprocess.CalledProcessError:
        return False


def clash_cmd(cmd):
    """执行 clash-ctl 命令"""
    try:
        result = subprocess.run(
            [CLASHCTL_BIN, cmd],
            capture_output=True, text=True, timeout=30,
            cwd=os.path.dirname(CLASHCTL_BIN)
        )
        return result.returncode, result.stdout, result.stderr
    except Exception as e:
        return 1, "", str(e)


def get_subscribe_url():
    """读取订阅链接"""
    try:
        with open(SUBSCRIBE_FILE, "r") as f:
            return f.read().strip()
    except:
        return ""


def set_subscribe_url(url):
    """保存订阅链接"""
    with open(SUBSCRIBE_FILE, "w") as f:
        f.write(url.strip() + "\n")


def get_current_node():
    """获取当前选中的节点名"""
    status, body = api_get("/proxies")
    if status != 200:
        return None
    try:
        data = json.loads(body)
        proxies = data.get("proxies", {})
        # 逐层找真实节点
        name = proxies.get("FINAL", {}).get("now")
        for _ in range(5):
            if not name:
                break
            item = proxies.get(name)
            if not item:
                break
            t = item.get("type", "")
            if t in ("Selector", "URLTest", "Direct", "Reject"):
                n = item.get("now")
                if n and n != name:
                    name = n
                else:
                    break
            else:
                break
        return name
    except:
        return None


# ============ HTTP 处理 ============

class Handler(SimpleHTTPRequestHandler):

    def log_message(self, format, *args):
        pass  # 静默访问日志

    def log_error(self, format, *args):
        """出错时输出到终端，便于调试"""
        sys.stderr.write("[ERROR] %s: %s\n" % (self.address_string(), format % args))
        sys.stderr.flush()

    def send_json(self, data, status=200):
        """返回 JSON 响应"""
        body = json.dumps(data, ensure_ascii=False)
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Content-Length", len(body.encode()))
        self.end_headers()
        self.wfile.write(body.encode("utf-8"))

    def proxy_mihomo(self, method, path, body=None):
        """代理请求到 mihomo"""
        try:
            import http.client
            conn = http.client.HTTPConnection(API_HOST, API_PORT, timeout=10)
            headers = {"Host": f"{API_HOST}:{API_PORT}", "Connection": "close"}
            if body:
                headers["Content-Type"] = "application/json"
                conn.request(method, path, body=body.encode(), headers=headers)
            else:
                conn.request(method, path, headers=headers)
            resp = conn.getresponse()
            ct = str(resp.getheader("Content-Type", ""))

            # /traffic: 读一行 JSON 返回（mihomo 每秒推送一次）
            if "text/event-stream" in ct or path.rstrip("/") == "/traffic":
                data = resp.readline()  # 只读第一行（约1秒内返回）
                conn.close()
                self.send_response(200)
                self.send_header("Content-Type", "application/json; charset=utf-8")
                self.send_header("Access-Control-Allow-Origin", "*")
                self.send_header("Content-Length", len(data))
                self.end_headers()
                self.wfile.write(data)
                return

            resp_body = resp.read()

            self.send_response(resp.status)
            for k, v in resp.getheaders():
                if k.lower() not in ("transfer-encoding", "connection"):
                    self.send_header(k, v)
            self.send_header("Access-Control-Allow-Origin", "*")
            self.send_header("Content-Length", len(resp_body))
            self.end_headers()
            if resp_body:
                self.wfile.write(resp_body)
            conn.close()
        except Exception as e:
            sys.stderr.write("[ERROR] proxy_mihomo: %s\n" % e)
            sys.stderr.flush()
            self.send_json({"error": str(e)}, 502)

    def do_GET(self):
        parsed = urlparse(self.path)
        path = parsed.path

        # API: 服务状态
        if path == "/api/status":
            node = get_current_node() if clash_running() else None
            self.send_json({
                "running": clash_running(),
                "node": node
            })
            return

        # API: 启动服务
        if path == "/api/start":
            code, out, err = clash_cmd("start")
            self.send_json({"ok": code == 0, "message": out + err})
            return

        # API: 停止服务
        if path == "/api/stop":
            code, out, err = clash_cmd("stop")
            self.send_json({"ok": code == 0, "message": out + err})
            return

        # API: 订阅 URL
        if path == "/api/url":
            self.send_json({"url": get_subscribe_url()})
            return

        # API: 代理到 mihomo
        if path.startswith("/api/"):
            mihomo_path = path[4:]  # 去掉 /api 前缀
            self.proxy_mihomo("GET", mihomo_path)
            return

        # 静态文件
        if path == "/" or not path.startswith("/api"):
            file_path = path.lstrip("/")
            if not file_path:
                file_path = "static/index.html"
            else:
                file_path = "static/" + file_path

            abs_path = os.path.join(os.path.dirname(__file__), file_path)
            if os.path.isfile(abs_path):
                self.path = "/" + file_path
                SimpleHTTPRequestHandler.do_GET(self)
            else:
                self.send_error(404)
            return

        self.send_error(404)

    def do_PUT(self):
        parsed = urlparse(self.path)
        path = parsed.path

        # 读取 body
        content_length = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(content_length).decode("utf-8", errors="replace") if content_length else ""

        # API: 保存订阅 URL
        if path == "/api/url":
            try:
                data = json.loads(body)
                set_subscribe_url(data.get("url", ""))
                self.send_json({"ok": True})
            except:
                self.send_json({"ok": False, "error": "invalid JSON"}, 400)
            return

        # API: 代理到 mihomo
        if path.startswith("/api/"):
            mihomo_path = path[4:]
            status = api_put(mihomo_path, body)
            self.send_json({"ok": status in (200, 204)}, status=200 if status in (200, 204) else status)
            return

        self.send_error(404)

    def do_OPTIONS(self):
        """CORS 预检"""
        self.send_response(200)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, PUT, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.end_headers()


# ============ 启动 ============

if __name__ == "__main__":
    print(f"clash-web 启动中，访问 http://localhost:{PORT}")
    print(f"按 Ctrl+C 停止")
    server = HTTPServer(("0.0.0.0", PORT), Handler)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n停止 clash-web")
        server.shutdown()
