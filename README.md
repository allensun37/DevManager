# DevManager

DevManager 是一个使用 C++17 和 CMake 构建的项目管理工具，提供命令行界面（CLI）和本地 HTTP API。

## v0.3 功能

- CLI 支持列出、新增、编辑和删除项目；删除只接受 `y/Y` 确认。
- CLI 支持按名称或技术栈搜索、按状态筛选，以及按 ID、名称或状态升序排序。
- 编辑会保留原项目 ID；删除后的 ID 永不复用。
- JSON 快照校验、候选状态保存、失败回滚和跨平台安全替换。
- HTTP server 提供项目的增删改查，默认监听 `127.0.0.1:8080`。
- GoogleTest 测试套件，以及 Ubuntu 和 Windows 上的 GitHub Actions CI。

## 依赖

- CMake 3.20 或更高版本。
- 支持 C++17 的编译器，例如 MinGW g++。
- 构建时由 CMake 获取 [nlohmann/json](https://github.com/nlohmann/json) 和 cpp-httplib。

## 构建与测试

在项目根目录执行：

```powershell
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

HTTP 可执行目标为 `devmanager_http`（Windows 下为 `devmanager_http.exe`）：

```powershell
cmake --build build --target DevManagerHttpServer --config Debug
```

如果本机网络无法让 CMake 下载 `nlohmann/json`，请准备该依赖的源码目录并额外传入：

```powershell
-DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON="<nlohmann-json-source-directory>"
```

## CLI 运行

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

## HTTP API

运行 HTTP server：

```powershell
.\build\devmanager_http.exe
```

默认监听 `127.0.0.1:8080`。路由如下：

| 方法 | 路径 | 说明 |
| --- | --- | --- |
| `GET` | `/api/projects` | 列出项目 |
| `POST` | `/api/projects` | 创建项目 |
| `PUT` | `/api/projects/{id}` | 更新项目 |
| `DELETE` | `/api/projects/{id}` | 删除项目 |

`POST` 和 `PUT` 使用 `Content-Type: application/json`，请求体示例：

```json
{
  "name": "DevManager",
  "techStack": ["C++", "CMake"],
  "description": "local project manager",
  "status": "active"
}
```

`GET /api/projects` 支持以下查询参数：`name`、`technology`、`status` 用于筛选，`sort` 用于排序（值为 `id`、`name` 或 `status`）。筛选参数最多一个；只提供 `sort` 时执行排序，筛选与排序同时提供时先筛选再排序。

错误响应的 `error.code` 可能为：`invalid_json`、`invalid_request`、`invalid_id`、`invalid_query`、`project_not_found`、`id_exhausted`、`persistence_failure`、`internal_error`。

CLI 和 HTTP server 不应同时写入 `data/projects.json`；请一次只运行一种写入入口。

## 数据文件

从项目根目录启动程序时，数据保存在：

```text
data/projects.json
```

文件首次不存在时会以空项目库启动；首次新增或删除项目后会创建或更新该文件。请勿手动写入损坏的 JSON：程序会报告错误，并保留原文件不覆盖。

## v0.3 非目标

本版本不包含认证（auth）、HTTPS/TLS、文件上传（uploads）、WebSocket、OpenAPI 文档、前端（frontend）、数据库、Redis、Docker、日志系统或可配置化（logging/config）等功能。
