# Quota Watch

M5Stack StopWatch 的 EspHub 原生应用。左右滑动或按侧键切换账号。内环显示 5 小时剩余额度，外环显示 7 天剩余额度。表盘中央显示邮箱、服务名和套餐，有余额的 ChatGPT 账号还会显示 credits。到期的数据等待下次导入，缺失值显示为 `--`。电脑配置页可查看 Codex 可用 Reset 次数。

## 安装

在已运行兼容 EspHub bootloader 的 StopWatch 上，通过原生程序 OTA 选择 Release 中 `esphub.json` 指向的应用镜像。镜像用于 ESP32-S3 StopWatch 的 4 MiB app 分区。按住 Select 开机可返回 EspHub bootloader。

[下载固件](https://github.com/e7h4n/quota-watch/releases/latest) · [打开蓝牙配置页](https://e7h4n.github.io/quota-watch/)

## 账号卡

电脑使用已登录的官方客户端采集只读快照，手表最多存储八个账号。导出器使用 Python 3.9+ 标准库。克隆仓库后在根目录运行以下命令。

### ChatGPT / Codex

每个账号使用各自的 Codex 档案。已有登录可直接导出。首次登录可执行：

```sh
export CODEX_HOME="$HOME/.codex-watch-a"
codex login
python3 tools/account_card.py codex --out "$HOME/Downloads/codex-watch-a.json"
```

导出器通过本地 Codex app-server 的 `account/read` 和 `account/rateLimits/read` 获取邮箱、套餐、周期、余额及可用重置次数。使用第二个账号时选择对应的 CODEX_HOME 和输出文件。

### Claude

在独立 Claude Code 档案里登录。把以下 statusLine 配置合入该档案的 settings.json，替换两个绝对路径。已有自定义状态栏时，可在原脚本中把同一份标准输入转交给导出器。输出文件每次以原子替换方式更新。第一次带用量信息的状态栏事件到达后，就可以导入。

```json
{
  "statusLine": {
    "type": "command",
    "command": "python3 '/absolute/path/quota-watch/tools/account_card.py' claude --out '/absolute/path/claude-watch-a.json'"
  }
}
```

导出器读取当前档案的 `claude auth status --json` 和 Claude Code 推送给状态栏的 `rate_limits`。请在同一 CLAUDE_CONFIG_DIR 中运行 Claude 和状态栏。不同账号用独立的输出文件。使用官方客户端本身的登录，导出文件只包含显示字段和标识，不含 OAuth 凭据。

### 同步到手表

用电脑的 Chrome 或 Edge 打开配置页。应用运行期间可以随时连接，配对时在系统窗口输入表上六位码。选择对应的账号卡并点「写入手表」，页面会读回校验。再次导入同一标识会更新已有快照，更早的快照会被拒绝。额度变动后重新导出并写入；手表独立计算重置倒计时，电脑断开后仍能查看最后同步的数据。配置页断开仅结束 BLE 会话，移除操作只清除手表上的该条记录。

## 构建

```sh
cd firmware
python3 fetch_drivers.py
idf.py -DIDF_TARGET=esp32s3 -DPROJECT_VER=dev build
```

使用 ESP-IDF v5.5.2。主分支 CI 校验应用描述符、芯片、版本和镜像大小，生成 SHA-256 及 EspHub 安装清单。板级显示、触摸和原生打包适配来自 [Coffee Watch](https://github.com/e7h4n/coffee-watch)，驱动采用固定版本的 M5GFX、M5IOE1、M5PM1。
