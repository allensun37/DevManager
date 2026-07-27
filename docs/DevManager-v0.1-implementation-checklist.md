# DevManager v0.1 实现前检查清单

**目标：** 在不超出 v0.1 范围的前提下，完成可持久化的 C++17 命令行个人项目管理工具。

**已确认边界：** 支持列表、新增、删除、名称搜索、技术栈搜索和 JSON 持久化；暂不实现编辑、MySQL、HTTP API 和 Web 页面。

## 开始前：GitHub 与 Git 身份

- [x] 创建并使用新的 GitHub 账号；不复用旧账号。
- [x] 为该账号创建 DevManager 的远程仓库，仓库名称使用 `DevManager`。
- [x] 在本机为本项目配置新的 Git 提交用户名和邮箱，避免沿用旧账号身份。
- [x] 使用 SSH 密钥或 GitHub Personal Access Token 完成认证；不得将令牌、密码或私钥提交到仓库。
- [x] 首次推送后，确认 GitHub 提交记录显示为新账号。

## 架构约束

- [x] 菜单层只处理循环菜单、输入和输出。
- [x] `ProjectManager` 只处理业务规则、项目集合和 ID 分配。
- [x] `Project` 只表示项目数据及其 JSON 转换。
- [x] JSON 文件读写封装在仓储层，不写入菜单层或 `ProjectManager`。
- [x] 业务层仅依赖仓储接口，不能依赖 JSON 细节。

## 实施顺序

### 阶段 1：工程骨架

- [x] 初始化 Git 仓库。
- [x] 建立 `src`、`include`、`data`、`tests` 目录。
- [x] 配置 CMake、C++17 和 nlohmann/json 依赖。
- [x] 验证空工程可配置、构建和运行。
- [x] 提交：`chore: initialize CMake project structure`

### 阶段 2：领域模型

- [x] 定义 `Project`：ID、名称、技术栈标签、描述、自由文本状态。
- [x] 明确字段规则：名称和状态非空；技术栈至少一个非空标签；描述可空。
- [x] 设计 JSON 中项目对象的固定字段名。
- [x] 为字段校验和 JSON 往返转换准备测试用例。
- [x] 提交：`feat: add project domain model`

### 阶段 3：业务逻辑

- [x] 实现内存中的项目新增、删除、列表和查询。
- [x] 自动生成并维护不复用的永久 ID。
- [x] 实现名称和技术栈的忽略大小写部分匹配。
- [x] 将 `C++` 与 `cpp` 的规范化规则集中在一个位置。
- [x] 覆盖空输入、无效 ID、删除后 ID 不复用等边界测试。
- [ ] 提交：`feat: implement project management operations`

### 阶段 4：JSON 持久化

- [x] 定义仓储接口与 JSON 实现。
- [x] 在 `data/projects.json` 保存 `nextId` 和 `projects`。
- [x] 文件缺失时加载空库。
- [x] JSON 损坏时返回明确错误，且不覆盖原文件。
- [x] 验证重启后的项目数据与 `nextId` 能正确恢复。
- [x] 提交：`feat: add JSON project persistence`

### 阶段 5：命令行交互

- [x] 实现循环菜单和退出流程。
- [x] 对数字输入、空行、错误 ID 提供可重试的提示。
- [x] 删除前要求 y/n 确认。
- [x] 列表结果明确显示真实项目 ID。
- [x] 验证成功和失败路径都不会导致程序异常退出。
- [x] 提交：`feat: add interactive command-line menu`

### 阶段 6：发布准备

- [x] 执行全部测试与手动菜单验收。
- [x] 检查中文文本、技术栈标签和 JSON 文件的显示与保存。
- [x] 在 README 记录构建、运行和数据文件位置。
- [x] 创建 `v0.1.0` 标签。

## 必测场景

- [x] 新增项目后重启，数据仍存在。
- [x] 删除一个项目后，新项目得到新的 ID 而非被删除的 ID。
- [x] `server` 能匹配 `HTTP Server`。
- [x] `cpp` 能匹配标签 `C++`。
- [x] 项目名称或状态为空时不能新增。
- [x] 技术栈全为空标签时不能新增。
- [x] `projects.json` 缺失时可正常启动。
- [x] `projects.json` 损坏时程序提示错误且不覆盖该文件。
