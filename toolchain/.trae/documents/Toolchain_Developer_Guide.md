# ABACUS Toolchain 开发者文档

版本: v4.0  (与当前代码实现一致)
更新时间: 2025-10-20

---

## 1. 架构设计说明

ABACUS toolchain 采用分层与模块化架构，主脚本专注流程编排，核心能力下沉到独立模块，安装流程按阶段分解：

- 主控脚本
  - `scripts/install_abacus_toolchain_new.sh`：入口与流程控制。负责配置初始化、参数解析、配置校验、环境导出与阶段安装调用。
- 核心模块 (`scripts/lib/`)
  - `config_manager.sh`：配置系统与参数解析，集中管理 `CONFIG_CACHE`。
  - `config_validator.sh`：语义与系统校验，冲突检测与错误分组报告。
  - `package_manager.sh`：阶段化安装调度、系统需求检查、依赖编排。
  - `version_helper.sh` / `version_loader.sh`：版本展示、校验与变量加载。
  - `user_interface.sh`：统一输出、横幅、美化与兼容性（Unicode fallback）。
  - `error_handler.sh`：一致的错误处理与堆栈辅助。
  - `tool_kit.sh`：通用工具与解析辅助（如 `read_with`、`read_enable`、版本比较）。
- 阶段目录 (`scripts/stage0-4/`)
  - `stage0`：编译工具与编译器（gcc/intel/amd, cmake, buildtools）。
  - `stage1`：MPI实现（mpich/openmpi/intelmpi）。
  - `stage2`：数学库（mkl/aocl/openblas）。
  - `stage3`：科学计算库（fftw/libxc/scalapack/elpa 等）。
  - `stage4`：可选组件（libnpy/libri/libcomm/cereal/rapidjson/libtorch/nep）。
- 版本与常量
  - `scripts/package_versions.sh`：集中版本源；`scripts/common_vars.sh`：通用变量。

数据流与交互关系：
- 主脚本调用 `config_init()` → `config_parse_arguments()` 生成 `CONFIG_CACHE`。
- `config_validator.sh` 在配置与环境维度进行分组校验，返回错误组与警告组计数与详细行。
- `config_export_to_env()` 将 `CONFIG_CACHE` 映射为环境变量并写入 `SETUPFILE`。
- `package_manager.sh` 依据配置执行 `package_install_stage()` 顺序调用 `stage0→stage4` 安装脚本。
- `version_loader.sh` 在安装前加载各包版本变量；`version_helper.sh` 提供版本展示与校验工具。

核心文件产物：
- `SETUPFILE` 与 `toolchain.conf`：导出环境与配置快照，供后续构建与复现。

---

## 2. 核心功能实现

- 配置管理（`config_manager.sh`）
  - 原理：使用关联数组 `CONFIG_CACHE` 作为单一真源，O(1) 读写；兼容旧脚本选项语义。
  - 关键实现：`config_set_defaults` 设定默认值；`config_parse_arguments` 解析所有 `--with-*`、`--enable-*`、模式与数值参数；`config_export_to_env` 将配置规范化导出。
  - `--package-version` 修复：支持多键值对与两种写法（`--package-version a:b c:d` 或多次传参）。解析为 `CONFIG_CACHE["PACKAGE_VERSION_<PKG>"]` 的 `main/alt` 值，并提供严格格式校验与错误提示，消除静默失败。

- 配置校验（`config_validator.sh`）
  - 原理：把逻辑错误按“错误组”聚合，区分“错误组计数”与“错误行展示”，避免虚高计数。
  - 关键实现：
    - 数学库与MPI冲突：模式互斥与组合合法性。
    - 编译器一致性：GCC/Intel/AMD 模式与工具链联动检查。
    - CMake 条件检查与版本校验：`--with-cmake=install/system/no` 分支；系统 cmake 需 ≥3.16（`validate_cmake_version`）。
    - 系统增强：glibc 版本摘要、GPU 架构/目标 CPU 合法性、必要依赖存在性。
  - 输出：错误组/警告组计数 + 逐行详细指导（含解决建议与替代方案）。

- 包管理（`package_manager.sh`）
  - 原理：阶段化驱动，按 `stage0→stage4` 顺序执行；对每阶段进行系统需求预检与失败短路；支持 `--install-all`、`--dry-run`、`--pack-run`。
  - 关键实现：`package_check_system_requirements`、`package_install_stage`；离线打包 `pack-run` 支持完整重用与镜像回退策略（先官方源，失败回退镜像）。

- 版本系统（`version_helper.sh` / `version_loader.sh`）
  - 原理：版本单独管理，避免硬编码；支持主/备用通道（`main/alt`）。
  - 关键实现：智能横幅版本显示；`version_show_available` 展示可用版本；加载到脚本环境用于下载与构建。

- 界面与错误（`user_interface.sh` / `error_handler.sh`）
  - 原理：统一输出、三级 Unicode fallback、美观横幅；错误集中处理与可跟踪。
  - 关键实现：友好 GCC 版本错误提示、解决策略推荐；一致日志前缀与等级。

- 通用工具（`tool_kit.sh`）
  - 解析辅助：`read_with`、`read_enable`；比较：`version_compare`；路径与下载通用函数。

- 阶段脚本
  - 设计：各脚本自洽且幂等（多次执行不破坏环境），遵循环境变量输入与明确输出路径。

---

## 3. 依赖管理规范

- 单一真源：`scripts/package_versions.sh` 统一维护所有包版本与下载 URL 模板；`version_loader.sh` 在运行时加载。
- 通道机制：每包支持 `main/alt`；由 `--package-version` 或默认策略决定。
- 下载源策略：优先官方源，失败自动回退镜像；保留校验与重试逻辑（面向 `pack-run` 与离线场景）。
- 约束边界：各阶段明确依赖集，禁止跨阶段隐式耦合；新增依赖需显式声明并通过校验器规则。
- 引入新依赖标准流程：
  1) 在 `package_versions.sh` 登记版本与通道；更新 `version_loader.sh` 加载；
  2) 在对应 `stageX/install_<pkg>.sh` 编写安装逻辑（保持幂等、可恢复与可检测）；
  3) 在 `config_validator.sh` 增加模式/冲突规则；必要时扩展系统预检；
  4) 在 `README.md`/开发者文档补充使用与迁移说明；
  5) 通过 `--dry-run`、最小化集成测试与离线 `pack-run` 验证。

---

## 4. 扩展开发指南

- 新功能模块添加规范（lib 层）
  - 创建 `scripts/lib/<module>.sh`，提供纯函数接口；禁止引入全局可变状态，配置通过 `CONFIG_CACHE` 与显式参数传递。
  - 在主脚本注册初始化与调用点；必要时扩展 `config_export_to_env` 的映射。

- 新安装包/阶段扩展
  - 在对应阶段新增安装脚本；遵循：可重入与幂等、显式依赖、失败短路与清晰日志。
  - 在 `package_manager.sh` 注册阶段驱动；必要时更新系统预检。

- 持续重构建议
  - 保持“好品味”原则：消除边界情况、简洁优先、实用主义与细节精确性。
  - 合并重复逻辑到工具层；跨阶段约束通过校验器统一表达。

- 兼容性保障措施
  - 维持旧选项映射与 README 迁移表；新增功能默认不破坏旧行为。
  - 错误组机制避免噪声计数；任何弃用行为以警告形式发布并给出替代方案。

---

## 5. 最佳实践

- 配置为中心：`CONFIG_CACHE` 是唯一真源，导出与持久化统一在 `SETUPFILE/toolchain.conf`。
- 无静默失败：所有解析与校验必须有清晰错误与指导；避免“成功但未生效”。
- 幂等安装：阶段脚本可重复执行且能检测已有构件；下载与构建具备恢复能力。
- 版本集中：版本更新只改 `package_versions.sh`；测试同时覆盖 `main/alt`。
- 日志统一：使用 `user_interface.sh` 提供的输出；错误经 `error_handler.sh` 处理。
- 检查先行：在安装前进行系统与冲突预检；`--dry-run` 用于快速验证；`pack-run` 用于离线复现。
- 贡献规范：小步提交与清晰描述；跨模块改动需同步更新文档与校验器。

### Sanitizer 抑制文件（.supp）与环境集成
- 目的：在启用 AddressSanitizer/LeakSanitizer（ASan/LSan）与 ThreadSanitizer（TSan）时，屏蔽第三方库或工具链已知的泄漏/竞态噪声，减少误报，提高诊断效率。
- 生成位置：
  - `scripts/stage0/install_gcc.sh:255–274` 生成 `install/lsan.supp` 与 `install/tsan.supp`
  - `scripts/stage1/install_mpich.sh:190–196` 追加 MPICH 相关泄漏抑制条目到 `lsan.supp`
- 环境导出：
  - 在 `install/setup:12–13` 中导出 `LSAN_OPTIONS=suppressions=<path>` 与 `TSAN_OPTIONS=suppressions=<path>`，运行时自动生效
- 使用建议：
  - 优先在 ASan+LSan 模式下使用 `LSAN_OPTIONS` 的抑制文件；TSan 通过 `TSAN_OPTIONS` 使用抑制规则（如 `race:`、`deadlock:`、`mutex:`）
  - 抑制项仅用于第三方与工具链已知噪声，不应用于掩盖自有代码问题；规则匹配尽量精确，避免过宽抑制
  - 默认禁用 TSAN 的生产安装不会受 `.supp` 文件影响，可安全保留作为“随时可用”的调试设施

---

## 架构细节补充

- 模块调用图：`install_abacus_toolchain_new.sh` → `config_manager.config_init` → `config_manager.config_parse_arguments` → `config_validator.validate_configuration` → `config_manager.config_export_to_env` → `package_manager.package_install_all` → `package_manager.package_install_stage(stage0→stage4)` → 阶段脚本。
- 数据边界：主脚本仅读取与写入 `CONFIG_CACHE` 与环境映射；模块间通过函数参数传递，不共享隐式全局状态。
- 幂等性：阶段脚本在检测到已安装目标或已存在构件时跳过重做；下载与构建逻辑具备重试与恢复能力。
- 运行产物：生成 `SETUPFILE`（环境导出）与 `toolchain.conf`（配置快照）；供后续 `toolchain_*.sh` 与 `build_abacus_*.sh` 使用。

## 配置数据字典

- 模式类键：
  - `mpi_mode`：`mpich|openmpi|intelmpi`。
  - `math_mode`：`mkl|aocl|openblas`。
  - `gpu_ver`：GPU 架构版本号（如 `80`）。
  - `target_cpu`：CPU 目标微架构（如 `skylake`）。
- 资源/并发：
  - `jobs`：并行编译进程数（来自 `-j`）。
  - `log_lines`：日志截取行数（用于用户界面友好展示）。
- 行为开关：
  - `dry_run`：`yes|no`，仅校验与展示不执行安装。
  - `pack_run`：`yes|no`，离线打包与离线安装支持。
  - `install_all`：`yes|no`，一次性执行 `stage0→stage4`。
- 选择器（`with_*`）：
  - `with_gcc|with_intel|with_amd|with_cmake`：`__SYSTEM__|__INSTALL__|__DONTUSE__`（或 `no` 等价 `__DONTUSE__`）。
- 使能（`enable_*`）：
  - 典型键：`enable_fftw|enable_libxc|enable_scalapack|enable_elpa|enable_nep|enable_libtorch|enable_libri|enable_libnpy|enable_libcomm|enable_cereal|enable_rapidjson`，取值 `yes|no`。
- 版本覆盖：
  - `PACKAGE_VERSION_<PKG>`：如 `PACKAGE_VERSION_OPENMPI=alt`、`PACKAGE_VERSION_CMAKE=main`，取值 `main|alt`。
- 路径与产物：
  - `INSTALL_PREFIX|INSTALL_DIR`：工具链安装根路径；`SETUPFILE`：环境导出文件；`CONFIG_FILE`：`toolchain.conf` 保存位。
- 环境检测：
  - `CRAY_ENV`：`yes|no`，在 CRAY 环境进行额外处理。

## 选项映射与示例

- 基本模式：
  - `--mpi-mode=openmpi`、`--math-mode=openblas|aocl|mkl`、`--gpu-ver=80`、`--target-cpu=skylake`。
- 行为与并发：
  - `-j 16` 控制并行；`--dry-run` 预检流程；`--install-all` 全流程；`--pack-run` 离线打包与安装。
- 资源选择：
  - `--with-cmake=install|system|no`；编译器同理：`--with-gcc=install|system|no`、`--with-intel=...`、`--with-amd=...`。
- 版本覆盖：
  - 单个：`--package-version openmpi:alt`。
  - 多个（单参）：`--package-version openmpi:alt cmake:main elpa:alt`。
  - 多个（多参）：`--package-version openmpi:alt --package-version cmake:main --package-version elpa:alt`。
- 常用组合示例：
  - GNU + OpenMPI + OpenBLAS：`--with-gcc=install --mpi-mode=openmpi --math-mode=openblas --install-all`。
  - Intel + IntelMPI + MKL：`--with-intel=system --mpi-mode=intelmpi --math-mode=mkl --install-all`。
  - AOCC + OpenMPI + AOCL：`--with-amd=install --mpi-mode=openmpi --math-mode=aocl --install-all`。

## 校验器错误组

- cmake_missing：`--with-cmake=system` 且系统未找到 `cmake`；建议安装或改用 `--with-cmake=install`。
- cmake_disabled：`--with-cmake=no`；提示 CMake 为必需，提供启用方案。
- cmake_version_unknown：无法解析 `cmake --version`；提示检查 PATH/安装。
- cmake_version_too_old：系统 CMake 版本低于 `3.16.0`；建议升级或安装工具链版本。
- compiler_conflict：编译器模式与工具链选择不一致或互斥；提示统一到单一编译器方案。
- mpi_conflict：MPI 模式与已选择的库不兼容或重复选择；提示选择单一实现。
- math_conflict：`mkl/aocl/openblas` 冲突或与其它库组合不合法；给出调整建议。
- gpu_version_invalid：`--gpu-ver` 非法或未支持；提示合法值范围与禁用方案。
- system_requirement_missing：缺失关键系统依赖（如 `glibc` 版本过低等）；给出安装命令与替代。

## 版本与通道策略

- 版本集中：统一在 `scripts/package_versions.sh` 维护；运行时由 `version_loader.sh` 加载。
- 通道选择：每包 `main|alt` 两通道；默认由策略决定，`--package-version` 可覆盖。
- 展示与校验：在欢迎横幅与摘要中展示版本；验证版本满足最低要求并与模式一致。

## 阶段与约束

- `stage0`：编译器与 CMake；确保至少一个编译器与可用 CMake（系统或工具链）。
- `stage1`：MPI 实现；只能选择一个实现；与编译器 ABI 保持一致性。
- `stage2`：数学库；`mkl/aocl/openblas` 三选一；依赖 FFTW/ScaLAPACK 联动。
- `stage3`：科学计算库；按 `enable_*` 选择安装；ELPA 与 MKL/ScaLAPACK 的链接需一致。
- `stage4`：可选组件；独立使能；不应引入对前述阶段的强制耦合。

## Pack-Run 离线模式

- 目标：在无网络或受限网络环境完成工具链安装与复现。
- 行为：在下载阶段智能打包 `prerequisites` 与源代码归档；安装阶段优先使用本地打包资源，失败再回退网络源。
- 建议流程：
  - 首次联网环境执行：`--install-all --pack-run` 生成离线包并完成安装。
  - 后续离线环境：重复同样命令，将自动复用打包资源；必要时加 `--dry-run` 预检。

## ELPA 与 MKL 修复

- 背景：在早期脚本中，ELPA 安装可能因 MKL 环境未正确导出或链接顺序不当导致失败。
- 现状：工具链在选择 `math_mode=mkl` 时，统一导出关联环境并规范链接次序；校验器也会在 ELPA 使能时检测必要前提。
- 建议：若自定义构建，请确保相关变量（如 `MKLROOT` 与库路径）在 `SETUPFILE` 中正确导出；避免同时选择不兼容的数学库组合。

## UI 兼容性

- 输出统一：通过 `user_interface.sh` 统一前缀与等级，保持跨终端一致性。
- Unicode 三级回退：优先使用图标；若终端不支持则退化到安全字符与 ASCII，避免乱码。
- 诊断友好：GCC 版本错误与系统摘要输出提供具体修复建议与命令示例。

## 测试与验证

- 预检：优先使用 `--dry-run` 查看配置摘要与校验报告。
- 最小安装：仅启用所需阶段与库，缩小失败面。
- 组合矩阵：建议覆盖 GNU/OpenMPI/OpenBLAS，Intel/IntelMPI/MKL，AOCC/OpenMPI/AOCL 三条主线；在 GPU/CPU 不同目标下交叉测试。
- 版本覆盖：同时验证 `main|alt` 通道；在 `pack-run` 环境做一次离线安装演练。

## 贡献与风格

- 原则：坚持“好品味”——消除边界情况、简洁优先、实用主义、细节精确。
- 风格：bash 脚本统一缩进与命名；函数提供清晰接口与返回码；避免跨模块共享隐式状态。
- 扩展：新增功能先在 `lib/` 完成封装，再在主脚本编排；规则与依赖在校验器与版本系统中同步维护。
- 文档：对外行为或默认变更必须同步更新 `README.md` 与本开发者文档。


## 模块 API 参考（按代码实现）

下列 API 列表基于 `scripts/lib` 目录的实际实现，按模块分组，包含函数职责、关键副作用（写入的配置键或导出的环境变量）以及返回约定。用于后续开发、扩展与故障定位。

### config_manager.sh（配置管理器）
- `config_manager_init()`
  - 作用：仅初始化一次配置；调用 `config_set_defaults`、`config_load_from_file`、`config_apply_modes_from_file`；设置 `CONFIG_INITIALIZED=true`。
  - 副作用：写入大量 `CONFIG_CACHE[*]` 键；可能输出 UI 信息。
  - 返回：始终 `0`。
- `config_set_defaults()`
  - 作用：统一设置默认值；包括 `with_*` 置为 `__DONTUSE__`，工具默认（`with_gcc=__SYSTEM__`、`with_cmake=__INSTALL__`）、并行数 `NPROCS_OVERWRITE` 自动探测、`MATH_MODE=openblas` 及相应 `with_*`、MPI 自动探测并设置 `MPI_MODE` 与 `with_mpich/openmpi/intelmpi`，默认库与开关（`with_fftw/libxc/scalapack/elpa/...`、`enable_*`）、版本策略 `VERSION_STRATEGY=main`，以及 CLE 环境特殊处理。
  - 副作用：大量默认键写入 `CONFIG_CACHE`；可能调用 `ui_info` 或输出警告。
  - 返回：`0`。
- `read_enable_option(arg)`
  - 作用：解析 `--enable-X[=yes|no]` 样式值；返回 `__TRUE__/__FALSE__/__INVALID__`。
  - 返回：字符串（供调用方使用）。
- `read_with_option(arg)`
  - 作用：解析 `--with-PKG[=install|system|no|path]` 样式值；返回内部枚举或路径。
  - 返回：字符串（`__INSTALL__/__SYSTEM__/__DONTUSE__` 或自定义路径）。
- `config_validate()`
  - 作用：校验 `NPROCS_OVERWRITE`、`LOG_LINES` 为数字；`GPUVER` 仅支持数字格式（如 `8.0/80/89`），并派生 `ARCH_NUM`；如不合法，输出错误并 `exit 1`。
  - 副作用：设置 `ARCH_NUM`；可能中止进程。
  - 返回：`0`（正常路径）。
- `config_apply_env_logic()`
  - 作用：按环境逻辑修正互斥/联动：Intel/AMD 与 GCC 安装互斥；MPI 关闭则禁用 `scalapack/elpa`；安装 GCC 则要求安装对应 MPI 并禁用 Intel 编译器与 Intel MPI；根据 `MPI_MODE` 强制仅启用一种 MPI；`MATH_MODE=mkl` 禁用 `openblas/scalapack/fftw`；GPU 开启时强制 `--gpu-ver` 合法；`with_cmake` 不得为 `__DONTUSE__`。
  - 副作用：覆盖若干 `with_*` 与模式键；必要时 `exit 1`。
  - 返回：`0`（正常路径）。
- `config_export_to_env()`
  - 作用：将 `CONFIG_CACHE` 全量导出为环境变量；导出 `tool_list/mpi_list/math_list/lib_list/package_list`。
  - 副作用：`export` 环境键。
  - 返回：`0`。
- `config_get(key)` / `config_set(key, value)` / `config_has(key)`
  - 作用：读/写/判定 `CONFIG_CACHE`。
  - 返回：`config_get` 输出值；`config_set/config_has` 返回 Bash 约定。
- `config_print_summary()`
  - 作用：打印配置摘要（模式、CPU/GPU、并行数、包状态）。
  - 返回：`0`。
- `config_parse_arguments "$@"`
  - 作用：统一解析 CLI：
    - 帮助与版本：`-h|--help` → `show_help=__TRUE__`；`--version` → `show_version=__TRUE__`；`--version-info [pkg]` → `show_version_info="all"|<pkg>`。
    - 构建：`-j <N>` → `NPROCS_OVERWRITE`；`--dry-run` → `dry_run=__TRUE__`；`--pack-run` → `PACK_RUN=__TRUE__`。
    - 版本：`--package-version` 支持 `--package-version=...` 或多对 `PKG:VER`（空格分隔）→ `PACKAGE_VERSION_<PKGUPPER>=main|alt`，严格格式校验与错误提示。
    - 配置文件：`--config-file FILE` → 调用 `config_load_from_file`。
    - 模式：`--mpi-mode mpich|openmpi|intelmpi|no` → `MPI_MODE`；`--math-mode mkl|aocl|openblas|cray|no` → `MATH_MODE` 并对 `aocl` 自动设定 `with_fftw/scalapack=__SYSTEM__`。
    - 包控制：`--with-<pkg>[=install|system|no|path]`，特殊处理 `mpich-device`（写 `MPICH_DEVICE` 并设 `MPI_MODE=mpich`）、`intel-classic`（`yes/no`）、`intel-mpi-classic`（`yes/no`）、`ifx`（`yes/no`）、`flang`（`yes/no`），以及 `mpich/openmpi/intelmpi/mkl/openblas/aocl/fftw/scalapack` 的显式标记数组 `USER_EXPLICIT_MPI/MATH` 防止被模式覆盖；`4th-openmpi` 兼容（写 `OPENMPI_4TH=yes|no`）。
    - 功能开关：`--enable-<feature>[=yes|no]` → `enable_<feature>=__TRUE__/__FALSE__`。
    - 其他：`--gpu-ver` → `GPUVER`；`--target-cpu` → `TARGET_CPU`；`--log-lines <N>` → `LOG_LINES`；`--skip-system-checks` → `skip_system_checks=__TRUE__`。
  - 返回：`0`；遇到非法输入时返回 `1` 并输出错误信息。
- `config_init "$@"`
  - 作用：完整初始化序列：`config_set_defaults` → `version_helper_init`（如在 PATH 中）→ `config_load_from_file` → `config_apply_modes_from_file` → `config_parse_arguments` → `config_apply_modes` → `config_validate`。
  - 副作用：配置键写入、校验与可能中止。
  - 返回：`0`。
- `config_apply_modes()` / `config_apply_modes_from_file()`
  - 作用：根据 `MPI_MODE/MATH_MODE` 归一化 `with_*`；尊重 `USER_EXPLICIT_*` 防止覆盖；对 `mkl/aocl/openblas/cray` 施加默认 `with_*` 行为与 FFTW/ScaLAPACK 禁用策略（`mkl`）。

### config_validator.sh（配置校验器）
- `config_validator_init()`：初始化校验状态数组与分组列表。
- `add_validation_error(msg)` / `add_validation_warning(msg)` / `add_validation_info(msg)`：记录错误/警告或即时信息。
- `start_error_group(name)` / `start_warning_group(name)`：启动分组计数（逻辑问题粒度）。
- `add_validation_error_group(name, ...)` / `add_validation_warning_group(name, ...)`：以组形式追加多行详情并计数。
- `validate_math_libraries()`：确保 `mkl/aocl/openblas` 至多一个启用；`mkl` 下对 `fftw/scalapack` 发出建议性警告。
- `validate_mpi_implementations()`：确保 `mpich/openmpi/intelmpi` 至多一个启用。
- `validate_compiler_consistency()`：`gcc/intel/amd` 启用数量检查；Intel 启用时提示 `mkl/intelmpi` 建议。
- `validate_system_requirements()`：
  - `with_cmake=__DONTUSE__` 则报致命错误组：要求启用或安装 CMake；
  - `with_cmake=__SYSTEM__` 时：检测系统 `cmake` 是否存在与版本满足（≥3.16），版本解析失败或过旧给出修复建议（安装指定版本，如 `${cmake_ver:-3.29.6}`）。
  - 其他系统工具缺失（`make/git/wget/tar/gzip`）与开发包提示（`build-essential/gcc/g++/gfortran`）。
  - GCC 工具链一致性：检测 `gcc/g++/gfortran` 是否齐全、版本提取是否成功、三者版本是否一致、主版本是否满足（≥5），并给出修复路径（安装 `${gcc_ver:-13.2.0}`）。
- `validate_logical_consistency()`：`elpa` 需要 `scalapack`（或 `mkl` 作为代替）；`scalapack` 启用但无 MPI 则警告；开启 GPU 时建议 ELPA 的 GPU 支持。
- `validate_package_versions()`：版本兼容性占位检查；对未指定 GCC 版本给出告警。
- `validate_configuration()`：串行执行所有检查，打印收集到的错误/警告详细列表；按分组计数返回非零（存在错误组时返回 `1`）。
- `should_skip_validation()`：当环境变量 `SKIP_SYSTEM_CHECKS=true` 时跳过校验（注意与 CLI 键名不一致，见下文兼容性）。

### package_manager.sh（包管理器）
- `package_manager_init()`：确保配置管理器就绪，定义依赖关系。
- `package_manager_define_dependencies()`：分阶段（0..4）建立包依赖图（编译器/构建工具→MPI→数学库→科学库→高级库）。
- `package_is_enabled(pkg)`：`with_pkg` 为 `__INSTALL__/__SYSTEM__` 视为启用。
- `package_get_mode(pkg)`：返回 `with_pkg` 的当前模式值。
- `package_mark_built(pkg)`：标记包已构建（写 `PACKAGE_BUILD_STATUS[pkg]=built`）。
- `package_get_dependencies(pkg)`：输出依赖列表（空格分隔）。
- `package_check_dependencies(pkg)`：检查依赖是否启用且已构建；引用未在本文件定义的 `package_is_built`（见兼容性注意）。
- `package_install_stage(stage)`：按阶段调用 `${SCRIPTDIR}/stage${stage}/install_stage${stage}.sh`；支持 `dry_run`；调用前导出版本配置；失败时报错并返回 `1`。
- `package_install_all()`：正常路径依次安装 stage 0..4；`PACK_RUN=__TRUE__` 时仅检查系统需求并返回。
- `package_check_system_requirements()`：检查系统工具（`wget/curl/tar/gzip/make`）；随后以 `MATH_MODE` 进行编译器可用性告警分支（设计上更合理应依据 `with_*`，见兼容性注意）。
- `package_write_config(config_file)`：生成包含列表与 `with_*` 状态的配置文件；记录模式与目标 CPU/GPU 版本。
- `package_list_to_install()` / `package_get_system_list()` / `package_print_summary()`：汇总计划构建与系统使用包列表并打印摘要。
- `package_validate_config()`：检查 MPI/数学库冲突与 GPU 使能一致性；返回错误计数。
- `package_clean_build()` / `package_clean_install()`：清理/创建构建与安装目录。
- `package_install_all_pack_run()`：在 Pack-Run 离线模式下顺序执行所有阶段脚本（0..4）。
- `package_export_version_config()`：导出版本配置到环境：
  - `ABACUS_TOOLCHAIN_VERSION_SUFFIX=alt|main` 基于 `use_alt_versions`（注意与 `VERSION_STRATEGY` 的差异，见兼容性注意）；
  - `ABACUS_TOOLCHAIN_PACKAGE_VERSIONS` 基于 `CONFIG_CACHE` 中的所有 `PACKAGE_VERSION_*` 键；
  - 兼容导出 `VERSION_SUFFIX`。
- `package_install_all_stages()`：顺序调用 `stage0..stage4/install_stageX.sh`，与原链路一致。

### user_interface.sh（用户界面）
- 初始化与能力检测：`ui_detect_unicode_support()`、`ui_init_icons()`、`ui_init([verbose|quiet|--log-file=PATH])`。
- 输出与日志：`ui_print_color(color, msg, [icon])`、`ui_info/success/warning/error`、`ui_debug`（仅 `UI_VERBOSE=true` 时）。
- 系统信息与版本：`ui_get_glibc_version()`（多策略探测）与 `ui_get_version()`（读取 `VERSION` 文件）。
- 视觉组件：`ui_welcome_banner()`、`ui_section(title)`、`ui_progress(current,total,desc,[eta])`。
- 帮助与摘要：`ui_show_help()`（详细选项与示例、推荐工作流）、`ui_show_summary()`（系统信息、配置与包表）、`ui_confirm_installation()`（交互确认）。
- 安装过程提示：`ui_stage_progress(stage,name)`、`ui_package_progress(pkg,action)`（start/download/extract/configure/compile/install/success/skip/error）。
- 结果与环境：`ui_show_results(success,total,failed_list)`、`ui_show_env_setup()`。
- 中断与信号：`ui_handle_interrupt()`、`ui_setup_signals()`。
- 输入校验：`ui_validate_input(input,type)` 支持 `number/path/file/mpi_mode/math_mode/gpu_version`。

### version_helper.sh（版本助手）
- 信息与帮助：`version_show_available([pkg])`、`version_show_package_info(pkg)`、`version_show_help()`。
- 交互选择：`version_interactive_select()`（按包选择 `main/alt` 并写入 `CONFIG_CACHE[PACKAGE_VERSION_*]`）。
- 版本决策：`version_get_effective(pkg)` 按优先级返回 `specific → global VERSION_STRATEGY → legacy OPENMPI_4TH → main`。
- 加载变量：`version_load_package_vars(pkg)` 将有效版本转换为后缀并调用 `load_package_vars`；导出 `<PKG>_EFFECTIVE_VERSION`。
- 显示与校验：`version_show_current()` 打印全局策略与包级覆盖；`version_validate_config()` 校验所选版本是否在版本表中存在；`version_helper_init()` 设定缺省策略与兼容旧变量（`OPENMPI_4TH=yes`）。

### version_loader.sh（版本加载器）
- `load_package_with_version(pkg)`：依据 `ABACUS_TOOLCHAIN_PACKAGE_VERSIONS` 或 `ABACUS_TOOLCHAIN_VERSION_SUFFIX` 选择后缀并调用 `load_package_vars`；支持 `VERBOSE_MODE=__TRUE__` 打印调试。
- `has_version_config()`：检测环境是否存在版本配置。
- `get_package_version_suffix(pkg)`：返回包的有效后缀（默认 `main`）。
- `show_version_debug(pkg)`：打印版本选择的调试信息（全局后缀、包级映射与最终后缀）。

### error_handler.sh（错误处理）
- 初始化与陷阱：`error_handler_init()` 设置 `set -e` 与 `trap 'error_handler ${LINENO}' ERR`。
- 主错误处理：`error_handler(line,[code])` 打印出错位置、失败命令与调用栈并以错误码退出。
- 报错/警告：`report_error([line], msg, [context])`、`report_warning([line], msg, [context])`。
- 增强接口：`report_error_enhanced(code, msg, [context], [line])` 返回指定错误码；`report_warning_enhanced(severity, msg, [context])`。

## CLI 选项 → 配置键映射一览

- 基本：
  - `-h|--help` → `show_help=__TRUE__`
  - `--version` → `show_version=__TRUE__`
  - `--version-info [pkg]` → `show_version_info="all"|<pkg>`
  - `-j <N>` → `NPROCS_OVERWRITE=<N>`（整数）
  - `--dry-run` → `dry_run=__TRUE__`
  - `--pack-run` → `PACK_RUN=__TRUE__`
- 版本策略：
  - `--package-version PKG:main|alt`（支持多对、等号或空格形式）→ `PACKAGE_VERSION_<PKGUPPER>`
  - `VERSION_STRATEGY=main|alt`（配置文件或内部）
  - 兼容：`--with-4th-openmpi=yes|no` → `OPENMPI_4TH=yes|no`
- 模式选择：
  - `--mpi-mode mpich|openmpi|intelmpi|no` → `MPI_MODE`（伴随强制唯一 `with_*`）
  - `--math-mode mkl|aocl|openblas|cray|no` → `MATH_MODE`（`mkl` 禁用 `fftw/scalapack`；`aocl` 将二者设为 `__SYSTEM__`）
- 包控制（`--with-PKG`）：
  - `mpich/openmpi/intelmpi` → `with_*` 并设定 `MPI_MODE`（非 `__DONTUSE__`）与显式标记 `USER_EXPLICIT_MPI[*]`
  - `mkl/openblas/aocl/fftw/scalapack` → `with_*` 与显式标记 `USER_EXPLICIT_MATH[*]`
  - 特殊布尔：`intel-classic` → `intel_classic=yes|no`；`intel-mpi-classic` → `INTELMPI_CLASSIC=yes|no`；`ifx` → `WITH_IFX=yes|no`；`flang` → `WITH_FLANG=yes|no`
  - 设备：`mpich-device=ch3|ch4` → `MPICH_DEVICE` 并设 `MPI_MODE=mpich`
  - 路径：任意非枚举值按路径（`~` 展开为 `$HOME`）处理
- 功能开关：
  - `--enable-FEATURE[=yes|no]` → `enable_FEATURE=__TRUE__/__FALSE__`
- 资源/并发：
  - `--gpu-ver <NUM|NUM.NUM>` → `GPUVER`（经 `config_validate` 转换 `ARCH_NUM`）
  - `--target-cpu <CPU>` → `TARGET_CPU`
  - `--log-lines <N>` → `LOG_LINES`（整数）
- 其他：
  - `--config-file <FILE>` → `config_load_from_file(FILE)`
  - `--skip-system-checks` → `skip_system_checks=__TRUE__`（兼容说明见下）

## 兼容性与已知注意事项（基于当前代码）

- 校验跳过键名不一致：
  - CLI 写入的是 `skip_system_checks=__TRUE__`（小写加下划线，值为枚举），而 `config_validator.sh` 的 `should_skip_validation()` 检查的是环境变量 `SKIP_SYSTEM_CHECKS=true`（大写布尔字串）。
  - 建议：如需跳过校验，当前可在环境中显式 `export SKIP_SYSTEM_CHECKS=true`；或统一键名与布尔约定（后续修复）。
- 包构建状态检测缺失：
  - `package_check_dependencies()` 引用未定义的 `package_is_built`。
  - 建议：在包管理器内补充 `package_is_built(pkg)`，或改为依据 `PACKAGE_BUILD_STATUS[pkg]` 与目标产物存在性判定。
- 编译器可用性告警逻辑：
  - `package_check_system_requirements()` 以 `MATH_MODE` 分支检查 `gcc/intel/amd`，与设计期望不符（应检查 `with_gcc/with_intel/with_amd`）。
  - 当前影响：该分支通常不会触发告警，但不影响安装流程。
- 版本传递不完全对齐：
  - `package_export_version_config` 依据 `use_alt_versions` 设置 `ABACUS_TOOLCHAIN_VERSION_SUFFIX`；若仅设置 `VERSION_STRATEGY=alt` 而未设置 `use_alt_versions`，则全局后缀可能仍为 `main`。
  - 个别阶段脚本若只依赖环境后缀而不读取 `ABACUS_TOOLCHAIN_PACKAGE_VERSIONS`，会导致全局 `alt` 未生效。
  - 建议：阶段脚本统一依赖 `version_loader.sh` 并优先解析 `ABACUS_TOOLCHAIN_PACKAGE_VERSIONS`；同时将 `package_export_version_config` 对齐为检测 `VERSION_STRATEGY`。

## 阶段脚本接口与版本加载约定

- 调用序列：`package_install_all()` 或 `package_install_all_stages()` 将依次执行 `stage0..stage4/install_stageX.sh`。
- 版本环境：在执行阶段脚本前会导出：
  - `ABACUS_TOOLCHAIN_VERSION_SUFFIX=alt|main`
  - `ABACUS_TOOLCHAIN_PACKAGE_VERSIONS="pkg1:main pkg2:alt ..."`
  - 兼容：`VERSION_SUFFIX` 与各 `PACKAGE_VERSION_*` 仍保留在环境。
- 阶段脚本建议：统一 `source lib/version_loader.sh` 并以 `load_package_with_version <pkg>` 取代直接 `load_package_vars <pkg>`，以确保尊重全局与包级版本选择。

## 全局数据结构与键空间约束（摘录）

- `CONFIG_CACHE[with_*]`：安装模式枚举 `__INSTALL__/__SYSTEM__/__DONTUSE__` 或路径。
- `CONFIG_CACHE[MPI_MODE/MATH_MODE/TARGET_CPU/GPUVER/ARCH_NUM/NPROCS_OVERWRITE/LOG_LINES]`：模式与资源键。
- `CONFIG_CACHE[enable_*]`：功能开关枚举 `__TRUE__/__FALSE__`。
- 版本策略：`CONFIG_CACHE[VERSION_STRATEGY]`（`main|alt`），包级覆盖：`CONFIG_CACHE[PACKAGE_VERSION_<PKGUPPER>]`。
- 兼容键：`OPENMPI_4TH=yes|no`（legacy）。
- 显式标记：`USER_EXPLICIT_MPI[*]`、`USER_EXPLICIT_MATH[*]` 防止模式覆盖用户选择。
- 列表：`tool_list/mpi_list/math_list/lib_list/package_list`（由配置管理器维护与导出）。

## 落地建议（针对文档与代码的对齐）

- 在阶段脚本统一引入 `version_loader.sh`，用 `load_package_with_version` 读取版本后缀；避免手写后缀分支。
- 将校验跳过键统一为单一约定（建议：`SKIP_SYSTEM_CHECKS=true|false`），同时 `--skip-system-checks` 写入该环境键。
- 增补 `package_is_built(pkg)` 或调整依赖检查到产物文件判断；在 `package_mark_built` 调用处保证一致。
- 将 `package_export_version_config` 同步尊重 `VERSION_STRATEGY`（当其为 `alt` 时强制 `ABACUS_TOOLCHAIN_VERSION_SUFFIX=alt`）。
- 将编译器可用性告警改为检查 `with_gcc/with_intel/with_amd`，而非 `MATH_MODE`。

## 重构执行摘要（来自进展报告的整合）

- 模块化架构完成：7个核心模块与阶段脚本职责清晰。
- 主脚本精简：流程编排集中，配置/校验/安装分离。
- 错误处理统一：集中错误处理与栈信息，提升追踪。
- 版本管理完善：支持 `main/alt` 通道与包级覆盖；欢迎横幅显示版本。
- 向后兼容性：旧选项语义保留，新增功能默认不破坏旧行为。
- 集成测试与稳健性：核心组合矩阵运行通过；幂等与恢复能力增强。
- 用户界面优化：Unicode 回退、进度与摘要统一；GCC 版本错误提示更友好。
- 系统信息增强：`glibc` 版本与系统摘要更全面。
- `--package-version` 修复：支持多键值对与两种写法；严格格式校验与错误提示。
- CMake 检测与版本校验：条件检查、最低版本约束与修复建议。

## 验证方法论与覆盖度

- 功能映射验证：重构前各功能均在重构后找到对应实现。
- 参数处理验证：所有 CLI 选项按原语义解析；`--with-*`、`--enable-*`、模式与数值参数一致。
- 冗余消除检查：重复逻辑集中到模块；未使用函数清理，调用链闭环。
- 模块调用关系分析：主脚本→配置→校验→导出→安装的调用图与数据流明确。
- 覆盖度结论：选项处理、配置验证与冲突检查、环境导出与设置文件生成、阶段脚本调用顺序与行为与重构前保持一致或有所增强。

## 重构前后逻辑对比与改进效果

- 代码组织：单脚本→模块化；职责清晰、易维护。
- 参数处理：内联 while→`config_parse_arguments()`集中；易扩展。
- `--package-version`：单对、静默忽略→多对与双写法；严格校验、向后兼容。
- 错误处理：分散→统一模块；返回码一致、可追踪。
- 配置校验：内联→校验器模块；错误分组与详细建议。
- 系统检测：无条件/计数不准→条件+版本校验+组计数；报错更准确。
- 版本管理：硬编码→集中版本表与加载器；易更新、支持通道。

## 数据流转路径（配置→校验→安装）

- 配置数据流：`install_abacus_toolchain_new.sh` 调用 `config_manager` 生成并维护 `CONFIG_CACHE`，随后 `config_export_to_env` 写入环境与 `SETUPFILE`。
- 验证数据流：`config_validator.validate_configuration()` 读取 `CONFIG_CACHE` 与系统状态，输出错误组/警告组，用户界面打印详情与建议。
- 安装数据流：`package_manager.package_install_all()` 导出版本配置 → 逐阶段执行 `stageX/install_stageX.sh`；阶段脚本通过 `version_loader.sh` 解析包版本后缀。

## 重构优化路线图（新增）

- 统一校验跳过约定：将 CLI 与环境变量合一为 `SKIP_SYSTEM_CHECKS=true|false`，消除键名/布尔约定不一致。
- 增加 `package_is_built(pkg)` 与产物检测，完善依赖检查闭环，减少误报与漏报。
- 对齐版本导出：尊重 `VERSION_STRATEGY`，确保 `ABACUS_TOOLCHAIN_VERSION_SUFFIX` 与阶段脚本版本选择一致。
- 阶段脚本统一依赖 `version_loader.sh`，优先解析 `ABACUS_TOOLCHAIN_PACKAGE_VERSIONS`，避免仅依赖全局后缀。
- 错误组分类完善：定义稳定的错误组 taxonomy 与返回码策略，提升可观测性。
- 测试矩阵与烟雾测试：为 GNU/OpenMPI/OpenBLAS、Intel/IntelMPI/MKL、AOCC/OpenMPI/AOCL 三主线构建脚本化 smoke tests；覆盖 `pack-run` 离线场景。
- 日志与追踪增强：为关键阶段增加唯一事件ID，便于日志聚合与问题定位。
- 文档联动：将 README 的用户指南和开发者文档 cross-link，维护迁移表与弃用策略说明。
