# DevManager

DevManager 是一个使用 C++17 和 CMake 构建的命令行个人项目管理工具。

v0.2.0 支持：

- 列出、新增、编辑和删除项目；删除只接受 `y/Y` 确认。
- 按名称或技术栈搜索，按状态筛选，按 ID、名称或状态升序排序。
- 编辑保留原项目 ID；删除后的 ID 永不复用。
- JSON 快照校验、候选状态保存、失败回滚和跨平台安全替换。
- GoogleTest 测试套件，以及 Ubuntu 和 Windows 上的 GitHub Actions CI。

暂不支持 HTTP API、数据库、Redis、Docker 和 Web 页面。

## 依赖

- CMake 3.20 或更高版本。
- 支持 C++17 的编译器，例如 MinGW g++。
- 构建时由 CMake 获取 [nlohmann/json](https://github.com/nlohmann/json)。

## 构建与测试

在项目根目录执行：

```powershell
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

如果本机网络无法让 CMake 下载 `nlohmann/json`，请先准备该依赖的源码目录，再额外传入：

```powershell
-DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON="<nlohmann-json-source-directory>"
```

## 运行

从项目根目录运行：

```powershell
.\build\DevManager.exe
```

程序显示循环菜单：

```text
1. List projects
2. Add a project
3. Delete a project
4. Search by name
5. Search by technology
6. Edit a project
7. Filter by status
8. Sort projects
0. Exit
```

新增项目时，技术栈标签以英文逗号分隔，例如 `C++, CMake, Linux Socket`。

## 数据文件

从项目根目录启动程序时，数据保存在：

```text
data/projects.json
```

文件首次不存在时会以空项目库启动；首次新增或删除后会创建/更新该文件。不要手动修改损坏的 JSON 文件：程序会报告错误，并不会覆盖原文件。
