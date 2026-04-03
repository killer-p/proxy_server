# Makefile for Clash Control Tool
#
# 依赖: gcc（Ubuntu/Debian: sudo apt install build-essential）
#
# 编译:
#   make
#
# 一键部署（假设 mihomo 和 Country.mmdb 已在仓库中）:
#   make deploy

CC = gcc
CFLAGS = -Wall -Wextra -O2
LDFLAGS =

# 目标文件
TARGET = clash-ctl
SRC = clash-ctl.c

# 默认目标
.PHONY: all
all: $(TARGET)
	@echo "编译完成!"

# 编译主程序
$(TARGET): $(SRC)
	@echo "编译 clash-ctl..."
	@$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

# 清理
.PHONY: clean
clean:
	rm -f $(TARGET) .clash.pid clash.log

# 更新二进制文件提示
.PHONY: download
download:
	@echo "二进制文件（mihomo、Country.mmdb）已在仓库中。"
	@echo "如需更新，请手动下载并替换："
	@echo "  mihomo:       https://github.com/MetaCubeX/mihomo/releases"
	@echo "  Country.mmdb: https://github.com/Loyalsoldier/geoip/releases"

# 一键部署: 编译 + 设置权限
.PHONY: deploy
deploy: $(TARGET)
	@chmod +x clash-ctl mihomo 2>/dev/null; true
	@echo ""
	@echo "部署完成!"
	@echo ""
	@echo "使用方式:"
	@echo "  ./clash-ctl set-url <订阅链接>  # 首次设置订阅"
	@echo "  ./clash-ctl start              # 启动代理"
	@echo "  ./clash-ctl status             # 查看状态"
	@echo "  ./clash-ctl list               # 列出节点"
	@echo "  ./clash-ctl select <编号>      # 切换节点"
	@echo ""
	@echo "其他设备配置代理:"
	@echo "  地址: <本机IP>:7890"
	@echo "  类型: HTTP"
	@echo ""

# 安装 (需要 root)
.PHONY: install
install: $(TARGET)
	@echo "安装 clash-ctl 到 /usr/local/bin/"
	cp $(TARGET) /usr/local/bin/
	@echo "安装完成!"

# 显示帮助
.PHONY: help
help:
	@echo "Clash Control Tool - Makefile"
	@echo ""
	@echo "目标:"
	@echo "  make          编译程序"
	@echo "  make clean     清理"
	@echo "  make deploy   一键部署（编译+权限）"
	@echo "  make install  安装到系统"
	@echo "  make help     显示此帮助"
	@echo ""
	@echo "提示: mihomo 和 Country.mmdb 已在仓库中，无需额外下载。"
	@echo "      如需更新，请访问上述下载地址手动替换。"
