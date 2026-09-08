# Board ID Option Bytes Fix Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reliably write board ID 201 to STM32F334R8 Option Bytes DATA0 and prevent stale calibration artifacts.

**Architecture:** Keep the existing first-boot calibration path, but unlock both the main Flash interface and Option Bytes through STM32F3 HAL before erase/program. Make the VS Code calibration task clean its isolated output directory before rebuilding, then validate the ELF and physical DATA0 value with J-Link.

**Tech Stack:** C++17, STM32F3 HAL, GNU Arm Embedded Toolchain 13.3, Make, SEGGER J-Link Commander.

## Global Constraints

- DATA0 must contain `0xC9` after calibration.
- Use `HAL_FLASH_Unlock()` followed by `HAL_FLASH_OB_Unlock()`; do not hardcode Option Keys.
- Preserve normal firmware behavior and the separate `build` and `build_cal` output directories.
- Do not include unrelated existing workspace changes in commits.
- Hardware verification replaces a mocked HAL unit test, as approved by the user.

---

### Task 1: Correct the STM32F3 Option Bytes unlock sequence

**Files:**
- Modify: `Core/Src/SuperCap.cpp:57-68`
- Test: `build_cal/Core/Src/SuperCap.lst`

**Interfaces:**
- Consumes: STM32F3 HAL functions `HAL_FLASH_Unlock()` and `HAL_FLASH_OB_Unlock()`.
- Produces: A calibration path where both `FLASH_CR.LOCK` and `FLASH_CR.OPTWRE` are cleared before option-byte erase/program.

- [ ] **Step 1: Record the failing behavior**

Run J-Link `mem32 0x1FFFF800 4` and confirm DATA0 is `0xFF`. Inspect the previous `build_cal/Core/Src/SuperCap.lst` and confirm it calls `HAL_FLASH_OB_Unlock()` without `HAL_FLASH_Unlock()`.

- [ ] **Step 2: Apply the minimal implementation**

Replace the hardcoded `FLASH->OPTKEYR` writes with:

```cpp
while (HAL_FLASH_Unlock() != HAL_OK)
    __ASM volatile("bkpt ");
while (HAL_FLASH_OB_Unlock() != HAL_OK)
    __ASM volatile("bkpt ");
```

Keep the existing erase, programming and reload sequence unchanged.

- [ ] **Step 3: Rebuild and inspect the calibration object**

Run:

```sh
make BUILD_DIR=build_cal clean
make -j8 BUILD_DIR=build_cal HARDWARE_ID=201 EXTRA_DEFS=-DCALIBRATION_MODE
```

Inspect `build_cal/Core/Src/SuperCap.lst`. Expected: calls to both HAL unlock functions, comparison with decimal 201, and no literals `0x08192A3B` or `0x4C5D6E7F`.

### Task 2: Prevent stale calibration macro values

**Files:**
- Modify: `.vscode/tasks.json:116`
- Test: `.vscode/tasks.json` plus two consecutive calibration builds with different source timestamps/macros as needed.

**Interfaces:**
- Consumes: VS Code input `hardwareID`.
- Produces: A calibration task that always deletes only `build_cal` before rebuilding and flashing.

- [ ] **Step 1: Confirm the stale-build risk**

The Make dependency files track headers and sources, not changes to command-line definitions such as `HARDWARE_ID` and `CALIBRATION_MODE`; the current task invokes only `make -j8`.

- [ ] **Step 2: Update the calibration command**

Use one shell chain:

```sh
make BUILD_DIR=build_cal clean && make -j8 BUILD_DIR=build_cal HARDWARE_ID=${input:hardwareID} EXTRA_DEFS=-DCALIBRATION_MODE && JLink ...
```

Do not clean the normal `build` directory.

- [ ] **Step 3: Validate configuration and firmware**

Parse `.vscode/tasks.json` as JSON-with-comments and confirm the calibration command contains the scoped clean before build. Rebuild board ID 201 and inspect the resulting listing.

### Task 3: Flash and verify the physical board

**Files:**
- Read: `tools/jlink_calflash.jlink`
- Read: `tools/jlink_readid.jlink`

**Interfaces:**
- Consumes: `build_cal/RM2024-SuperCap-F3-V1R.elf` for board ID 201.
- Produces: STM32F334R8 DATA0 low byte `0xC9`.

- [ ] **Step 1: Flash the rebuilt calibration ELF**

Run J-Link Commander with device `STM32F334R8`, SWD and `tools/jlink_calflash.jlink`.

- [ ] **Step 2: Allow option-byte reload and reconnect**

Reconnect after the calibration firmware triggers `HAL_FLASH_OB_Launch()` and the MCU resets.

- [ ] **Step 3: Read DATA0**

Run `tools/jlink_readid.jlink`. Expected output at `0x1FFFF804`: low byte `C9` rather than `FF`.

- [ ] **Step 4: Review and commit only scoped source/config changes**

Review `git diff -- Core/Src/SuperCap.cpp .vscode/tasks.json`, run `git diff --check`, and commit only those two paths after verification.
