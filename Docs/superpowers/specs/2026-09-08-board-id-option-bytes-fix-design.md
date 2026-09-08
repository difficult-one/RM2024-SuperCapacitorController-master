# 板号 Option Bytes 写入修复设计

## 背景与根因

STM32F334R8 的板号存放在 Option Bytes DATA0（`0x1FFFF804` 低字节）。当前板上读数为 `0xFF`，因此固件中的 `hardwareID` 为 255。

已烧录的校准固件只调用 `HAL_FLASH_OB_Unlock()`，没有先调用 `HAL_FLASH_Unlock()`。STM32F3 的 Option Bytes 擦除和写入要求同时解除主 Flash 接口的 `CR.LOCK` 和 Option Bytes 的 `CR.OPTWRE`，所以原流程不能完成写入。

当前工作区中的未提交修改虽然补充了主 Flash 解锁，却硬编码了不适用于 STM32F334 的 Option Key，也不能作为正确修复。

## 修复范围

1. 在校准模式下使用 STM32F3 HAL 提供的标准接口，按顺序调用：
   - `HAL_FLASH_Unlock()`
   - `HAL_FLASH_OB_Unlock()`
   - `HAL_FLASHEx_OBErase()`
   - `HAL_FLASHEx_OBProgram()`
   - `HAL_FLASH_OB_Launch()`
2. 删除硬编码 Option Key，使用芯片头文件和 HAL 中已经定义的正确密钥。
3. 修改 VS Code 的首次写板号任务，使其先清理 `build_cal`，再用指定 `HARDWARE_ID` 和 `CALIBRATION_MODE` 完整重建，避免复用由旧宏参数生成的目标文件。
4. 不修改正常运行固件的板号校验逻辑，不修改其他 Option Bytes 策略，也不做无关重构。

## 数据流

校准任务接收板号 201，清理并重建独立的 `build_cal` 目录。校准固件启动后读取 DATA0；若值不是 201，则依次解锁 Flash 和 Option Bytes、擦除并写入 DATA0，最后触发 Option Bytes reload 和系统复位。复位后再次读取时应得到 201，不再进入写入分支。

## 验证方案

这是依赖 MCU Flash 控制器的硬件行为，不引入模拟 HAL 的单元测试。采用以下可重复验证：

1. 修复前检查证明现有校准目标仅调用 `HAL_FLASH_OB_Unlock()`，能够稳定复现缺少主 Flash 解锁的问题。
2. 完整重建后检查 ELF/反汇编，确认包含 `HAL_FLASH_Unlock()` 和 `HAL_FLASH_OB_Unlock()`，且 `HARDWARE_ID` 为 201，不再包含错误的硬编码 Option Key。
3. 使用 J-Link 将校准 ELF 烧入已连接的 STM32F334R8。
4. 复位后回读 `0x1FFFF800` 至 `0x1FFFF80C`，确认 `0x1FFFF804` 低字节为 `0xC9`。
5. 再次运行或调试普通固件，确认 `hardwareID == 201`。

## 成功标准与安全边界

- DATA0 从 `0xFF` 变为 `0xC9`。
- 校准固件不再因写入失败持续进入校准/复位路径。
- 正常构建与校准构建继续使用相互独立的输出目录。
- 提交和修改仅覆盖本设计明确列出的文件，不纳入现有无关工作区改动。
