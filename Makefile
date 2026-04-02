# Makefile for Clash Control Tool
#
# 依赖: libcurl, json-c (可选)
#
# 安装依赖 (Ubuntu/Debian):
#   sudo apt install libcurl4-openssl-dev libjson-c-dev
#
# 编译:
#   make

CC = gcc
CFLAGS = -Wall -Wextra -O2
LDFLAGS = -lcurl

# 目标文件
TARGET = clash-ctl
SRC = clash-ctl.c

# 检查依赖库
UNAME_S := $(shell uname -s)

# 默认目标
.PHONY: all
all: $(TARGET)

# 编译主程序
$(TARGET): $(SRC)
	@echo "编译 clash-ctl..."
	@$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)
	@echo "编译完成!"

# 清理
.PHONY: clean
clean:
	rm -f $(TARGET) .clash.pid clash.log

# 下载 mihomo (Clash Meta 核心)
.PHONY: download
download:
	@echo "下载 mihomo (Clash Meta)..."
	@if command -v curl >/dev/null 2>&1; then \
		VERSION=$$(curl -sL "https://api.github.com/repos/MetaCubeX/mihomo/releases/latest" | grep tag_name | cut -d'"' -f4); \
		echo "最新版本: $$VERSION"; \
		curl -sL "https://github.com/MetaCubeX/mihomo/releases/download/$$VERSION/mihomo-linux-amd64-compatible-$$VERSION.gz" -o mihomo.gz; \
		gunzip -f mihomo.gz; \
		chmod +x mihomo; \
		echo "下载完成!"; \
	elif command -v wget >/dev/null 2>&1; then \
		VERSION=$$(curl -sL "https://api.github.com/repos/MetaCubeX/mihomo/releases/latest" | grep tag_name | cut -d'"' -f4); \
		echo "最新版本: $$VERSION"; \
		wget -q "https://github.com/MetaCubeX/mihomo/releases/download/$$VERSION/mihomo-linux-amd64-compatible-$$VERSION.gz" -O mihomo.gz; \
		gunzip -f mihomo.gz; \
		chmod +x mihomo; \
		echo "下载完成!"; \
	else \
		echo "错误: 请安装 curl 或 wget"; \
		exit 1; \
	fi

# 一键部署: 下载核心 + 编译 + 设置权限
.PHONY: deploy
deploy: download $(TARGET)
	@chmod +x mihomo $(TARGET)
	@echo ""
	@echo "部署完成!"
	@echo ""
	@echo "使用方式:"
	@echo "  ./clash-ctl start     # 启动代理服务"
	@echo "  ./clash-ctl status    # 查看状态"
	@echo "  ./clash-ctl list      # 列出节点"
	@echo "  ./clash-ctl select <节点名>  # 切换节点"
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
	@echo "  make clean    清理"
	@echo "  make download 下载 mihomo 核心"
	@echo "  make deploy   一键部署 (下载+编译)"
	@echo "  make install  安装到系统"
	@echo "  make help     显示此帮助"
