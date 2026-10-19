# GPIO 旋转编码器输入驱动

一个连接在两条 GPIO 线上的正交旋转编码器 Linux 驱动。驱动解码 A/B两相的状态跳变，并把每一个机械档位作为 `EV_REL` / `REL_X` 事件通过input 子系统上报。仓库还包含一套 gpio-sim 测试夹具，因此从 GPIO 边沿到 evdev 事件的完整链路可以在没有硬件的情况下验证。

这是一个学习规模的实现：platform 驱动、线程化中断、4x4 正交状态表，
以及一个带两个模块参数的 input 设备。

## 功能

- A、B 两相双边沿线程化中断
- 4x4 正交状态转换表，非法跳变直接忽略
- 累加四分之一拍，每满一格上报一次
- `steps_per_detent`（1-4）和 `invert` 两个模块参数
- gpio-sim 端到端测试，直接断言上报的事件

## 仓库结构

```text
Makefile
rotary_encoder_gpio.c		驱动源码
scripts/run_test.sh		测试启动脚本
tests/rotary_test.py		端到端测试脚本
README.md
LICENSE
.gitignore
```

## 硬件与接线

常见的机械编码器（例如 EC11）内部是两个共用公共端的开关 A 和 B：

| 编码器引脚 | 连接到 |
|------------|--------|
| A | GPIO A（带上拉） |
| B | GPIO B（带上拉） |
| C（公共端） | GND |

驱动把两条线都申请为普通输入，并且没有使能内部偏置，因此上拉必须由板子或设备树提供。随着 A、B 依次与公共端导通，线对会沿格雷码序列 `11 -> 01 -> 00 -> 10` 或其反方向移动。

当前模块把 GPIO lookup table 硬编码成了 gpio-sim 夹具。要在真实硬件上运行，需要修改这张表，或者把连线描述迁移到设备树。

## 环境要求

- Linux 6.8 x86_64（已测试；其他内核版本可能需要调整 API）
- 运行中内核对应的头文件
- 无硬件测试需要 `gpio-sim` 和 configfs
- Python 3.10+
- `insmod`、configfs 和 `/dev/input` 需要 root 权限
- Secure Boot：模块必须使用已注册的 MOK 密钥签名

## 构建

```bash
make
```

Secure Boot 开启时，加载前先签名：

```bash
sudo /usr/src/linux-headers-$(uname -r)/scripts/sign-file sha512 \
    /var/lib/shim-signed/mok/MOK.priv \
    /var/lib/shim-signed/mok/MOK.der \
    rotary_encoder_gpio.ko
```

## 无硬件测试

```bash
cd ~/drivers/rotary-encoder-input
sudo ./scripts/run_test.sh
```

测试脚本必须以 root 身份、在仓库根目录执行。它会构建并签名模块、加载gpio-sim、创建一个两线的模拟芯片、把两条线拉高、加载驱动，最后运行 Python 断言。

预期输出：

```text
clockwise one detent: expected [1], got [1]
counter-clockwise one detent: expected [-1], got [-1]
partial detent then back: expected [], got []
two detents clockwise: expected [1, 1], got [1, 1]
```


第一个用例同时验证了 `probe()` 中的初始电平采样：夹具从状态 `11` 起步，
如果驱动假设初始状态为 `0`，前几条边沿的方向会被解错，最终不会上报事件。

最简测试脚本有意不做清理。运行结束后模块和 configfs 夹具会留在系统里；
清理命令：

```bash
# 1. 卸载驱动，释放 GPIO 与中断
sudo rmmod rotary_encoder_gpio

# 2. 停用虚拟芯片
sudo sh -c 'echo 0 > /sys/kernel/config/gpio-sim/rotary-test/live'

# 3. 删除 configfs 对象
sudo rmdir /sys/kernel/config/gpio-sim/rotary-test/bank0/line0
sudo rmdir /sys/kernel/config/gpio-sim/rotary-test/bank0/line1
sudo rmdir /sys/kernel/config/gpio-sim/rotary-test/bank0
sudo rmdir /sys/kernel/config/gpio-sim/rotary-test
```

测试能证明：边沿序列、状态表、累加逻辑、上报方向和事件数量，以及
input 子系统的集成是否正确。

测试不能证明：电气时序、机械抖动，以及真实硬件上的行为。


## 设计说明

状态编码:`state = (A << 1) | B` 得到四个状态。状态转换表对合法的四分之一拍返回 `+1` 或 `-1`，对对角跳变（不可能发生）返回 `0`。

线程化中断: 处理函数运行在进程上下文，因此可以调用 `gpiod_get_value_cansleep()` 并使用互斥锁。因为注册时没有硬中断处理函数，所以必须使用 `IRQF_ONESHOT`。

互斥锁: 两条中断线共享同一份解码状态，因此对状态的读取、查表和事件上报必须串行化。

初始状态: `probe()` 在申请中断之前先采样两条线的电平。否则第一条边沿会基于错误的初始状态解码，导致第一个档位方向错误。

累加器: 累加的四分之一拍达到 `steps_per_detent` 时上报一次；反向转动会抵消未满一格的移动。


## 局限性

- GPIO lookup table 和 platform device 都是硬编码的，不支持设备树
- 没有软件消抖。线程化中断读取的是线电平而不是统计每个边沿，因此高速
  旋转可能丢边沿；抖动发生在累加器阈值附近时也可能误报一个档位
- 没有配置内部偏置，需要外部上拉
- 只上报相对 X 轴，没有绝对轴和唤醒支持
- 只实现了 `drivers/input/misc/rotary_encoder.c` 的一个最小子集
- 测试夹具没有覆盖抖动、真实硬件时序，以及 `steps_per_detent` /
  `invert` 参数组合

## 许可证

GPL-2.0-only，见 `LICENSE`。
