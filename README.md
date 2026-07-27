# DevManager

DevManager 是一个使用 C++17 和 CMake 构建的命令行个人项目管理工具。

v0.1 支持：

- 列出、新增和删除项目；删除前需要 `y/n` 确认。
- 按名称或技术栈搜索；搜索忽略 ASCII 大小写，`cpp` 可以匹配 `C++`。
- 保存项目、永久 ID 和下一个可分配 ID 到 JSON 文件。

暂不支持编辑、MySQL、HTTP API 和 Web 页面。

## 依赖

- CMake 3.20 或更高版本。
- 支持 C++17 的编译器，例如 MinGW g++。
- 构建时由 CMake 获取 [nlohmann/json](https://github.com/nlohmann/json)。

## 构建与测试

在项目根目录执行：

```powershell
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug
ctest --test-dir build --output-on-failure
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
0. Exit
```

新增项目时，技术栈标签以英文逗号分隔，例如 `C++, CMake, Linux Socket`。

## 数据文件

从项目根目录启动程序时，数据保存在：

```text
data/projects.json
```

文件首次不存在时会以空项目库启动；首次新增或删除后会创建/更新该文件。不要手动修改损坏的 JSON 文件：程序会报告错误，并不会覆盖原文件。
