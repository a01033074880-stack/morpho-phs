<!-- SPDX-License-Identifier: AGPL-3.0-only -->
# 专用池卡死复现记录（Phase 5b，2026-10-31，Asia/Shanghai，box）

- 现象（i9）：`p5_dedicated_cpuset` 第 2 轮 3 线程行 PASS 后无输出 16 分钟（14:22:47–14:38:46），进程 0 CPU，脚本中止。
- 根因：调用方等待 `done_` 时读两次：`while (done_.load()!=s) hybrid_wait(done_, done_.load(), …)`。t0 在两次读之间写入 `s`，调用方就去等 “`done_` ≠ s”，永远等不到 → 所有线程睡在 WaitOnAddress / futex 上。
- 复现 1：旧代码（`/tmp/old`，不进同步包），在两次读之间注入 300 µs 睡眠 → `test_phase5_stress` P1 在 0 块时看门狗报 HANG（退出码 2）。
- 复现 2：旧等待逻辑 + 新的插桩点位置（`/tmp/old2`，注意 `-I/tmp/old2` 必须排在 `-Iinclude` 之前）→ 170 块后 HANG。
- 修复后：同样注入（`-DSTRESS_HOOKS`）3 个种子 × 20 s/部分，全部通过；普通版 3 个种子 × 30 s/部分，全部通过（`test_phase5_stress*_s{1,2,3}.txt`）。
- 普通版（不注入）的旧代码在 box 上没有复现：窗口只有纳秒级；i9 上 MMCSS 工作线程在 `notify_all` 之后抢占普通优先级调用方，才把窗口撑开（推测，与现象一致）。
