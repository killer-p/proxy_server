# Makefile for Clash Control Tool
#
# 依赖: gcc（Ubuntu/Debian: sudo apt install build-essential）
#
# 编译:
#   make
#
# 一键部署:
#   make deploy

CC = gcc
CFLAGS = -Wall -Wextra -O2 -I.
LDFLAGS =
SRC = src/clash-ctl.c
LIBS = lib/cjson/cJSON.c

# 默认目标
.PHONY: all
all: app/clash-ctl
	@echo "编译完成!"

# 编译主程序
app/clash-ctl: $(SRC) $(LIBS)
	@mkdir -p app
	@echo "编译 clash-ctl..."
	@$(CC) $(CFLAGS) -o $@ $< $(LIBS)
	@echo "编译完成!"

# 清理
.PHONY: clean
clean:
	rm -rf app/clash-ctl app/.clash.pid app/clash.log

# 更新二进制文件提示
.PHONY: download
download:
	@echo "二进制文件（mihomo、Country.mmdb）已在仓库的 app/ 目录中。"
	@echo "如需更新，请手动下载并替换："
	@echo "  mihomo:       https://github.com/MetaCubeX/mihomo/releases"
	@echo "  Country.mmdb: https://github.com/Loyalsoldier/geoip/releases"

# 一键部署: 编译 + 设置权限
.PHONY: deploy
deploy: all
	@chmod +x app/clash-ctl app/mihomo 2>/dev/null; true
	@echo ""
	@echo "部署完成!"
	@echo ""
	@echo "使用方式:"
	@echo "  app/clash-ctl set-url <订阅链接>  # 首次设置订阅"
	@echo "  app/clash-ctl start              # 启动代理"
	@echo "  app/clash-ctl status             # 查看状态"
	@echo "  app/clash-ctl list               # 列出节点"
	@echo "  app/clash-ctl select <编号>      # 切换节点"
	@echo ""
	@echo "其他设备配置代理:"
	@echo "  地址: <本机IP>:7890"
	@echo "  类型: HTTP"
	@echo ""

# 安装 (需要 root)
.PHONY: install
install: all
	@echo "安装 clash-ctl 到 /usr/local/bin/"
	cp app/clash-ctl /usr/local/bin/
	@echo "安装完成!"

# 显示帮助
.PHONY: help
help:
	@echo "Clash Control Tool - Makefile"
	@echo ""
	@echo "目标:"
	@echo "  make          编译程序"
	@echo "  make clean    清理"
	@echo "  make deploy   一键部署（编译+权限）"
	@echo "  make install  安装到系统"
	@echo "  make fnpack   打包 fnOS 应用"
	@echo "  make help     显示此帮助"
	@echo ""
	@echo "提示: mihomo 和 Country.mmdb 已在 app/ 目录中，无需额外下载。"
	@echo "      如需更新，请访问上述下载地址手动替换。"

# fnOS 打包
FPK_DIR = clash-fnos
.PHONY: fnpack
fnpack: app/clash-ctl
	@# 同步最新编译产物到 fnOS 打包目录（保留 ui/ 等已有文件）
	@cp -rf app/* $(FPK_DIR)/app/
	@chmod +x $(FPK_DIR)/app/clash-ctl $(FPK_DIR)/app/server.py
	@chmod +x $(FPK_DIR)/cmd/*
	@echo "正在打包 fnOS 应用..."
	@cd $(FPK_DIR) && fnpack build
	@echo "打包完成"
	@# 复制到共享目录
	@cp $(FPK_DIR)/proxy_server.fpk /srv/samba/share/
	@echo "已复制到 /srv/samba/share/proxy_server.fpk"
