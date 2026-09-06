# RM2024 超级电容控制器 — 理解与使用规划书

> 目标读者:第一次接触本仓库、希望「大概理解 → 深刻理解 → 能独立使用与修改」的开发者。
> 本文是**路线图 + 阅读指引**,不是代码逐行讲解。每一步都给出「看什么文件、带着什么问题看、产出物是什么」。
> 本文提到的函数行号基于当前 master 版本,可能漂移;请用 `rg -n "函数名" Core/Src` 定位最新位置。

---

## 0. 项目一句话

这是 **RoboMaster 2024(港科大 ENTERPRIZE 战队)的超级电容控制器**:一块以 STM32F334 为核心、双向 DCDC(降压/升压)功率变换为核心的板载系统,在比赛中给底盘供电做「功率缓冲」——当裁判系统给的功率额度内电池电量不足/瞬时功率超过额度时,由超级电容补功率;额度富余时再把电容充满,全程由本控制器自动完成。

**必须先搞清的两个「侧」**(全仓库代码都围绕它们):
- **A 侧**:裁判系统 / 电池供电侧(约 24 V 母线,代码里叫 `vASide`)。
- **B 侧**:超级电容侧(代码里叫 `vBSide`,充满约 28.8 V,`MAX_CAP_VOLTAGE`)。
- 控制器是一个 **双向升降压变换器**:功率既可以 A→B(充电),也可以 B→A(放电补功率)。代码因此反复出现「Buck/Boost 模式切换」。

**你必须具备的前置知识**(按需补,缺哪补哪):
| 领域 | 需要到什么程度 |
|---|---|
| 电力电子 | 知道 Buck、Boost 是什么、占空比与电压关系;同步整流/两桥臂互补 PWM 概念 |
| STM32 基础 | 会用 CubeMX/HAL;理解中断、定时器、ADC+DMA、PWM 输出 |
| C/C++ | 能读 C++(本仓库只用 C++ 的 namespace/struct/类,无模板深水区) |
| 通信 | CAN 基本帧格式(标准帧 8 字节) |
| 调试 | 有 JLink/Ozone 或等效调试器(烧录与看变量用) |
| 竞赛规则 | RoboMaster 能量/功率规则(可选,但理解 0x061 字段语义需要) |

---

## 阶段 A — 大概理解(产出:一张系统框图 + 能讲 3 分钟)

**目标**:知道这个系统做什么、由哪些部分构成、代码文件如何分工、程序从哪跑到哪。

### A1. 先读文档与图
1. `readme.md`(开源说明、编译方法、硬件预览链接)。
2. `Docs/img_1.png` —— 硬件接口与连接图。
3. 硬件原理图(用 [KiCanvas](https://kicanvas.org/),链接在 readme):
   - `hardware/SuperCap2024_Control_..._ISOPWR`:控制板(MCU、采样、驱动、隔离电源)。
   - `hardware/SuperCap2024_Power_V1.2R`:功率板(功率管、电感、电容接口)。
   - 目的不是读懂每个电阻,而是找到:A/B 侧采样点、功率管驱动信号来自 MCU 的哪些引脚、板间连接器定义。

### A2. 仓库文件地图
用树形眼光看仓库,文件分为三类:
| 类别 | 文件 | 作用 | 概览阶段要不要读 |
|---|---|---|---|
| 项目工程 | `.ioc`、`Makefile`、`SuperCap-F3.mk`、两个 `.ld`、两个 `.s`、`.cproject` | CubeMX 工程 + 编译体系;`.ld`/`.s` 有普通版与 `_SuperCap` 改版 | 只读 readme 编译部分 |
| ST 驱动 | `Drivers/`、`Middlewares/` | HAL 库、CMSIS、DSP 数学库 | 不读 |
| CubeMX 生成 | `Core/Src/*.c`(main、adc、can、dma、gpio、hrtim、iwdg、tim、usart…) | 时钟/外设初始化 | 只读 `main.c` 的启动顺序 |
| **用户业务代码(重点)** | `Core/Src/SuperCap.cpp`、`PowerManager.cpp`、`Communication.cpp`、`IncrementalPID.cpp`、`Buzzer.cpp` + `Core/Inc/*.hpp` | 全部控制逻辑 | A 阶段读通主线 |

> ⚠️ 关键认知:**真正的“项目代码”是 `.cpp`**,`Core/Src` 里的 `.c` 大多是 CubeMX 生成的外设壳子。
> Makefile 用 `$(wildcard Core/Src/*.cpp)` 自动把 5 个 `.cpp` 编进来。

### A3. 走一遍启动过程(约 30 分钟)
1. `Core/Src/main.c` → `main()`:`HAL_Init` → 时钟配置(72 MHz?请自行确认)→ 初始化各外设 → 调用 `systemStart()` → 空 `while(1)`。
2. `Core/Src/SuperCap.cpp`(全文 106 行,一口气读完):
   - `init()`:延时等电压稳定 → 蜂鸣器初始化 → **读 Flash option bytes 里的 `HARDWARE_ID` 并与编译期 `-DHARDWARE_ID=xxx` 比对**,不匹配就 `bkpt`(这就是 readme 说“必须指定 HARDWARE_ID”的原因)→ 通信初始化 → `PowerManager::init()` → 启动 IWDG(看门狗)。
   - `loop()`:死循环只做一件事——喂狗。**全部实时工作都在中断里完成**,这是本项目的核心设计思想。
3. 由此建立心智模型:**主循环 = 空转喂狗;系统 = 三个中断驱动的事件源**。

### A4. 三条运行主线(画出你的框图)
读 `PowerManager.cpp` 末尾的 `extern "C"` 两个回调,以及 `Communication.cpp` 的 CAN 接收中断,建立三条线:

| 线 | 触发源 | 干什么 | 代码位置 |
|---|---|---|---|
| 高速控制环 | `HRTIM1_Master_IRQHandler`(随 PWM 周期,即「V I loop」) | 采 ADC → 算电压电流功率 → 短路检测 → 功率/电流/电压闭环 → 更新占空比 | PowerManager.cpp 970 行附近 |
| 慢速状态环 | `TIM2_IRQHandler`(约 1 ms?) | 更新状态量、查故障状态机、**通过 CAN 回传数据**、LED | PowerManager.cpp 1001 行附近 |
| 事件驱动 | `CAN_RX0_IRQHandler`(每帧裁判系统数据 0x061) | 更新功率额度/能量缓冲、开/关输出、**触发能量管理 updateEnergy** | Communication.cpp 86 行附近 |

A 阶段结束前,把这三条线 + A/B 侧 + MCU + 功率级画成一张你自己的框图,并对着框图讲 3 分钟,能讲通就过关。

---

## 阶段 B — 深刻理解(产出:能逐模块讲解、能回答“为什么这样做”)

**目标**:不只是知道“有这么个函数”,而是把数据流、控制层级、保护逻辑、标定来源、通信协议都钉死在代码行上。

### B1. 先理清「量」与数据流
`Core/Inc/PowerManager.hpp` 定义了几个全局结构体,是理解的中枢:
- `SampleManager::ProcessedData`(字段见 hpp 51–84 行):所有“物理量”`vASide / iASide / vBSide / iBSide / iReferee / pReferee / capCharge(?)` 等。注意注释里的**电流正方向约定**(67–69 行),方向错了整条链路都是反的。
- `ControlData::controlData`:来自裁判系统的输入(功率额度、能量缓冲、输出使能)。
- `Status::status`:给外部看的状态(错误码、电容能量占比、底盘功率、通信超时计数)。

主数据流(用 `rg` 顺着函数调用链验证):
```
ADC 原始值
  → ADCSample::sumBuffer()              8 次采样累加(Config.hpp ADC_SAMPLE_COUNT)
  → ProcessedData::updateAndCalibrate() 标定系数 → 物理量(滤波)
  → (高速环) handleShortCircuit + updateVIP
  → (慢环)   updateStatus / handleErrorState
  → (CAN)    打包 0x051 发回裁判/上位
```

### B2. 分模块深读清单(建议按顺序,每个模块回答列出的问题)

**① Config.hpp(全部宏)+ 阈值设计**
所有“魔法数”集中于此:`MAX_CAP_VOLTAGE`、`SHORT_CIRCUIT_*`、`I_LIMIT`、`OVER_TEMP_*`、`LOW_BATTERY_*`、`ENERGY_BUFFER`、`HRTIM_PERIOD`…
问题:每个宏对应硬件/规则上的什么?哪些是**可调保护阈值**,哪些是**物理不可改常数**?

**② PowerManager.cpp 导航**(建议按行号区间读,主线见下)
| 区间/函数 | 主题 | 要回答的问题 |
|---|---|---|
| 开头到 190 行 | 数据结构初值、三个 PID 对象 | 三个 PID(`pidPowerReferee` / `pidCurrentA` / `pidVoltageB`)各自管什么?参数注释里写了什么调试史? |
| `TimerManager`(约 25–70) | HRTIM 输出开关、TIM 配置 | HRTIM 的 TA/TB 两组输出对应功率级什么?`OENR/ODISR` 干什么? |
| `SampleManager`(约 71–165) | ADC 缓冲布局与标定 | 两块 ADC 各自采哪几路?`offset` 与通道数的对应关系? |
| `ErrorChecker::checkHardwareError`(约 209) | 上电硬件自检 | 检查哪些项?不过检就 `goto` 重来(见 `init`) |
| `handleShortCircuit`(约 290) | 短路保护 | 判据、动作、恢复策略 |
| `handleErrorState`(约 337) | 故障状态机 | 错误码位如何置/清?哪些故障可自恢复?超时/欠压如何收敛到安全状态? |
| `init`(595) | 初始化与自检循环 | `goto powerManagerInit` 的用意(反复自检直到通过) |
| `updatePWM`(637) | 占空比生成 | 传入量是“VB/VA 目标比”;看 **Buck/Boost 模式滞环切换**(0.8/0.9/1.111/1.25 边界)——为什么用滞环?占空比公式怎么来的? |
| `updateVIP`(682) | **主控环核心** | 见下方 B3 专题 |
| `updateStatus`(799) | 状态刷新 | `capEnergy` 为什么是 vB²/Vmax²(能量正比电压平方)?`communicationTimeoutCnt>=500` 时做什么(500 个慢环周期≈通信丢失回退)?PB14/PB15 两个 LED 的亮灭逻辑(错误/使能指示) |
| `updateEnergy`(861) | 能量“借用/偿还”管理 | `refereePowerLimit` 变化时为何 `baseRefereePower` 平移、`pidEnergyReferee` 如何把 `energyRemain` 拉回 `ENERGY_BUFFER` |

**③ updateVIP——全系统的心脏(B 阶段重点,精读 682–797 行)**
按序拆解这条“外环→限幅→内环”链:
1. 外环功率:目标 `targetRefereePower` 与实测 `pReferee` 进 `pidPowerReferee`(增量式),积分成 `targetAPower`(想从 A→B 输送的功率)。
2. 限幅:根据 B 侧电压(`<5V` 只给极小电流、`>12V` 才放开到 `I_LIMIT`)限制充/放电流,再换算成功率上下限,把 `targetAPower` 夹住——防止电容过充或电流超限。
3. 内环电流:`targetIA = targetAPower / vASide` → 与实测 `iASide` 进 `pidCurrentA` → 得占空比增量;还有一个带衰减的误差积分项 `extraErrorSum`(II 项)消除稳态误差。
4. 过压接管:当 `vBSide > MAX_CAP_VOLTAGE*0.9` 时切到 `pidVoltageB` 电压环限制输出,否则只是“记录误差不输出”(`updateDataNoOutput` 的用途:让 PID 状态在不需要时也跟随,避免切换冲击)。
5. `outputEnabled==false` 时走另一分支:占空比设成开环跟随 `vBSide/vASide`,所有 PID 只跟不输出(预充电/被动模式)。
6. 最后 `updatePWM(outputDuty)`。

**④ Communication.cpp(112 行,精读)**
- Rx 帧 **0x061**(8 字节,位域结构体 `RxData`):`enableDCDC`、`systemRestart`、`refereePowerLimit`(2B)、`energyRemain`(2B)。
- Tx 帧 **0x051**:`errorCode` + 底盘功率等 8 字节;注意 67–79 行是**手写寄存器发 CAN**(绕 HAL),为什么?(逐字节塞 mailbox,省开销)。
- 看门狗语义:`enableOutput` 的边沿使能逻辑、`powerLimit>250` 视为非法输入关输出、收帧即调用 `updateEnergy`。
问题:位域结构体的 `static_assert(sizeof==8)` 防什么?`__attribute__((packed))` 为何必要?

**⑤ IncrementalPID.cpp(48 行)+ MathUtil.hpp**
增量式 PID:只输出“本次增量 Δu”。思考:为何本项目全部用增量式?哪些地方用 `.update()`(要输出),哪些用 `.updateDataNoOutput()`(只维护内部状态)?

**⑥ 标定体系(会用的前提)**
- `Core/Inc/Calibration.hpp`:标定宏 `CALI_VA/CALI_VB/CALI_I_*`(线性:斜率+截距)。
- `Calibration/cal.xlsx`:战队做标定的 Excel 工具(采样读数→真实物理量 拟合)。
- 采样链路:运放/分压 → ADC → DMA 缓冲 → 平均 → 标定宏 → 物理量。
- `CALIBRATION_MODE` 宏(`SuperCap.cpp` 56–81 行):编译时定义它后,允许 MCU **把 HARDWARE_ID 写进 Flash option bytes**(首次烧录/换板必需)。

**⑦ 硬件/软件对照(建议用原理图对着代码)**
- ADC1 的 `VA / IReferee / IA`,ADC2 的 `VB / IB / NTC`(读 `adc.c` 与 PowerManager.cpp 141–146 行的缓冲偏移)。
- HRTIM 四个输出引脚 → 驱动芯片 → 功率管栅极;`hrtim.c` 里频率/死区配置,`HRTIM_PERIOD=16000` 的时基。
- `tim.c`:`TIM16` 用做“环运行耗时测量”(高速环里读计数器算 `load`),`TIM2` 做慢环时基。
- 独立看门狗 `IWDG`(`SuperCap.cpp` 30–40 行):超时复位——为什么主循环只喂狗。
- 蜂鸣器 `Buzzer.cpp`:启动/错误提示音(对照 `SuperCap::init` 里两次 `play`)。

### B3. 深挖作业(选做,做 1–2 个即可“毕业”)
1. 画出 HRTIM 一周期内的中断时序:采样发生在哪一时刻?为什么 VI 环能拿到“本周期”的电流而不是上一周期的?
2. 推导 updatePWM 里 Boost/Buck 模式占空比公式与滞环边界,证明它能让 VB/VA 收敛到目标(动笔推公式)。
3. 还原一次“超功率事件”全过程:裁判给 45 W 额度,底盘要 80 W → 逐行说明功率从 B 侧流向 A 侧时,`pidPowerReferee`、限幅、`pidCurrentA`、占空比如何联动,能量如何从 `energyRemain` 里“借”。
4. 找出所有“掉电/欠压 → 恢复”路径,说明为什么需要 `handleErrorState` 里的状态而不是简单 if-else。
5. 对比 `.cproject`/Makefile 与 `.ioc`:如果 CubeMX 重新生成代码,哪些 USER CODE 段会被保留?这套工程的“再生代码”工作流怎么走?

### B4. 自测:不看代码能答吗?
- 为什么 `main()` 之后系统“什么都不干”却能实时控制?
- `0x061` 里 4 个字段分别驱动哪条控制链?
- `updateVIP` 中电压环为什么只在 vB>0.9*Vmax 时才真正输出?
- 高速环和慢环各做哪类事?为什么不能全放一个环?
- 增量式 PID 的 `.update()` 与 `.updateDataNoOutput()` 差别在哪、各用在何时?

---

## 阶段 C — 会用(产出:能独立 编译→烧录→调试→改参→加功能→移植)

**目标**:不满足于读懂,而是能自己动手动刀。前提:先获得与作者相同或兼容的硬件/调试器;若只有开发板,则先做 C4 的软件侧演练。

### C1. 搭环境(Windows 为例,readme 已给,这里补全)
1. 装 **MSYS2**,把 `msys64/usr/bin` 加入 PATH;`pacman -S arm-none-eabi-gcc arm-none-eabi-newlib make`(或按 readme 装 arm-none-eabi-gcc)。
2. 装 **SEGGER J-Link + Ozone**(烧录/调试),或你熟悉的 `st-flash`/`OpenOCD`。
3. 验证:`arm-none-eabi-gcc --version`、`make --version`。
4. (可选)装 STM32CubeMX 打开 `.ioc` 查看外设配置;用 CLion/VS Code 读代码。

### C2. 编译
```bash
make -j HARDWARE_ID=1      # HARDWARE_ID 必须与目标板 option bytes 一致,否则上电 bkpt
# 产物:build/RM2024-SuperCap-F3-V1R.elf / .hex / .bin
make clean                 # 清理
```
> 探索任务:`Makefile` 里 `LDSCRIPT=STM32F334R8Tx_FLASH.ld`,而 readme 说当前使用 `*_SuperCap` 版链接脚本/启动文件。二者差异在哪?当前 master 实际生效的是哪套?(提示:比对两个 `.ld` 的 FLASH/RAM 布局与两个 `.s`。)——这是理解这套“双启动文件”体系的钥匙,建议先搞清楚再上电。

### C3. 首次烧录与上电(务必按顺序、加安全措施)
功率板有大电流与储能电容,接线只在断电下进行;先不加功率只烧程序验证,再分级加电。
1. J-Link 连 SWD,用 Ozone 新建工程加载 `.elf`,烧录。
2. **首次烧录/换新板**:需把 HARDWARE_ID 写入 option bytes。两条路:用调试器改 option bytes,或编译时加 `-DCALIBRATION_MODE`(见 `SuperCap.cpp` 的 `#ifdef CALIBRATION_MODE` 分支,程序会自写 DATA0 后重启)。注意:标定模式会擦写 option bytes,只应在确认需要的板子上用。
3. 上电预期:蜂鸣器短鸣(800 Hz)表示初始化走到 `PowerManager::init`;若长鸣(1000 Hz)后 `bkpt`,多半是 HARDWARE_ID 不匹配。
4. 用 Ozone 观察:`PowerManager::Status::status`(errorCode、outputEnabled)、`ProcessedData::processedData`(各物理量是否合理:采样值、电压极性)。
5. 示波器验证:HRTIM 输出 PWM(有无/占空比随负载变化)、A/B 侧电压。**先空载/小电流验证方向正确,再带载**。

### C4. 调参与开发练习(从小到大)
**练习 1(只改参数,最小风险)**:在 `Config.hpp` 调某个保护阈值,编译烧录,用上位机/CAN 观察 errorCode 变化。
**练习 2(测试通道)**:`PowerManager.cpp` 末尾 `namespace Test`(892 行起,`testICycle`)给 PID 定参数用;研究它的用法,把 `updateVIP` 换成 `updateVITest` 观察电流阶跃响应(参考注释“2024/5/15 调的,大概 5~10 ms”)。
**练习 3(PID 整定)**:按 外环慢→内环快 的原则,用练习 2 的通道整定 `pidCurrentA`,再用阶跃功率整定 `pidPowerReferee`;体会为什么增量式在嵌入式里好调。
**练习 4(加功能,结构性修改)**:新增一路 CAN Tx 上报某内部量(照抄 `feedbackPowerData` 的寄存器写法);或新增一种错误码并接入 `handleErrorState` 与 `Status::errorCode` 位定义。
**练习 5(搬动时基)**:把慢环从 TIM2 挪到 SysTick/其它定时器,体会外设解耦程度。

### C5. 移植指南(换 MCU/换板)
readme 明说:业务代码基本都在 `Core/Src` 的 `.cpp`,与 HAL 交互点集中在 `adc.h / hrtim.h / tim.h / can.h / iwdg.h`。
- 简单移植(同 F3 系列):新 CubeMX 工程 + 拷 `.cpp/.hpp` + 重新生成 `.c` 外设壳 + 核对引脚映射。
- 跨系列(如 G474):重点是 `hrtim.c`、`adc.c`(ADC 通道数/DMA 布局,PowerManager.cpp 141–146 行的缓冲 offset 与 `updateAndCalibrate` 的通道顺序)、时钟树;`Calibration.hpp` 标定系数要按新板采样网络重标。
- 移植验收:先跑 `checkHardwareError` 全绿,再小功率闭环。

### C6. 排错速查
| 现象 | 先查 |
|---|---|
| 上电 `bkpt` 卡死 | HARDWARE_ID 与 option bytes 不匹配;或 CALIBRATION_MODE 分支 |
| 有中断但不控 | `Status::status.outputEnabled`、`ControlData.enableOutput` 是否被 CAN 0x061 置位(没有上位/裁判帧时默认关输出!) |
| 电压/电流全 0 或乱跳 | ADC 缓冲 offset、DMA 通道顺序、`sumBuffer` 累加;用 Ozone 看原始 buffer |
| 功率方向反 | 电流正方向约定(hpp 注释)与标定符号 |
| 一会就复位 | IWDG 没喂(确认 `loop()` 在跑);或进了 `handleErrorState` 自复位逻辑 |
| 参数改了没效果 | 确认没有 `#ifndef CALIBRATION_MODE` 把分支裁掉;确认编的是当前文件(`make clean` 后重编) |

---

## 路线建议(弹性,按你每天 2–3 小时估)

| 里程碑 | 内容 | 对应阶段 | 完成标志 |
|---|---|---|---|
| 第 1 周 | 框图 + 三条主线 + 读完 SuperCap/Communication | A | 能讲 3 分钟、画出框图 |
| 第 2–4 周 | PowerManager 深读 + B3 作业 1–2 题 | B | 能对着代码讲清一次超功率事件 |
| 第 4–6 周 | 搭环境、烧录、观察状态、练习 1–3 | C | 板子能跑、参数自己动过手 |
| 之后 | 练习 4–5、移植或接自己队的上位机 | C+ | 完成一次结构性修改 |

## 参考资料
- 仓库内:`readme.md`、`Docs/`、`Calibration/cal.xlsx`、`.ioc`(外设配置的“真相”)、两个 `.ld`/`.s` 对比。
- ST:STM32F334 参考手册(HRTIM/ADC/CAN 章节)、HAL 文档(`Drivers/STM32F3xx_HAL_Driver` 自带注释)、ST 官网 HRTIM 应用笔记。
- 规则:RoboMaster 最新版能量/超级电容规则(理解 0x061 里功率额度和能量缓冲的赛场语义)。
- 硬件:KiCanvas 在线预览(见 readme),配合 KiCad 本地方便量测。

> 安全提醒:功率板工作在几十安培级、电容端电压可达 28.8 V 且储能可观。任何接线/测量前断电放电;首次上电用限流电源并串联功率电阻;调试功率级佩戴护目镜并远离电解电容正负极短路风险。软件之外的伤害,规划书救不了你。
