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
all: bin/clash-ctl
	@echo "编译完成!"

# 编译主程序
bin/clash-ctl: $(SRC) $(LIBS)
	@mkdir -p bin
	@echo "编译 clash-ctl..."
	@$(CC) $(CFLAGS) -o $@ $< $(LIBS)
	@echo "编译完成!"

# 清理
.PHONY: clean
clean:
	rm -f bin/clash-ctl bin/.clash.pid bin/clash.log

# 更新二进制文件提示
.PHONY: download
download:
	@echo "二进制文件（mihomo、Country.mmdb）已在仓库的 bin/ 目录中。"
	@echo "如需更新，请手动下载并替换："
	@echo "  mihomo:       https://github.com/MetaCubeX/mihomo/releases"
	@echo "  Country.mmdb: https://github.com/Loyalsoldier/geoip/releases"

# 一键部署: 编译 + 设置权限
.PHONY: deploy
deploy: all
	@chmod +x bin/clash-ctl bin/mihomo 2>/dev/null; true
	@echo ""
	@echo "部署完成!"
	@echo ""
	@echo "使用方式:"
	@echo "  bin/clash-ctl set-url <订阅链接>  # 首次设置订阅"
	@echo "  bin/clash-ctl start              # 启动代理"
	@echo "  bin/clash-ctl status             # 查看状态"
	@echo "  bin/clash-ctl list               # 列出节点"
	@echo "  bin/clash-ctl select <编号>      # 切换节点"
	@echo ""
	@echo "其他设备配置代理:"
	@echo "  地址: <本机IP>:7890"
	@echo "  类型: HTTP"
	@echo ""

# 安装 (需要 root)
.PHONY: install
install: all
	@echo "安装 clash-ctl 到 /usr/local/bin/"
	cp bin/clash-ctl /usr/local/bin/
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
	@echo "  make help     显示此帮助"
	@echo ""
	@echo "提示: mihomo 和 Country.mmdb 已在 bin/ 目录中，无需额外下载。"
	@echo "      如需更新，请访问上述下载地址手动替换。"
