# DevManager

DevManager 是一个使用 C++17 和 CMake 构建的小型项目管理工具，提供命令行界面（CLI）和本地 HTTP/JSON API。当前版本为 **v0.4.0**。

## v0.4.0 能力

- CLI：列出、新增、编辑、删除、名称/技术栈搜索、状态筛选和按 ID/名称/状态升序排序。
- HTTP：项目 CRUD、查询/筛选/排序，以及 `/health`、`/api/info` 和 `/api/statistics`。
- 配置：从 `config/devmanager.json` 读取服务、存储和日志配置；缺失配置文件时使用默认值。
- 日志：HTTP 服务使用固定版本的 spdlog 适配器记录启动、请求和错误信息。
- 请求追踪：HTTP 请求通过 `X-Request-ID` 关联；合法客户端值会被回传，缺失或不合法时由服务生成。
- 持久化：JSON 快照校验、候选状态保存、失败回滚以及跨平台安全替换。
- 测试与 CI：GoogleTest 测试套件，以及 Ubuntu 和 Windows 上的 GitHub Actions 自动构建、OpenAPI 校验和 CTest。

## 配置

默认配置文件位置是相对于进程工作目录的 `config/devmanager.json`。以下内容展示全部默认值：

```json
{
  "server": {
    "host": "127.0.0.1",
    "port": 8080
  },
  "storage": {
    "path": "data/projects.json"
  },
  "logging": {
    "level": "info",
    "path": "logs/devmanager.log"
  }
}
```

`storage.path` 和 `logging.path` 也相对于进程工作目录。配置文件不存在时使用上述默认值；JSON 损坏、根节点或字段类型不正确、空字符串以及端口不在 `1..65535` 范围内时，程序启动失败并返回非零状态。配置文件只负责运行参数，不包含数据库、配置中心或业务规则。

版本只有一个来源：CMake 的 `project(DevManager VERSION 0.4.0)`。构建过程生成 `DevManagerVersion.h`，HTTP `/api/info` 和测试使用该生成值。

## 依赖与范围

- CMake 3.20 或更高版本。
- 支持 C++17 的编译器（例如 GCC/MinGW 或 MSVC）。
- CMake FetchContent 固定获取 nlohmann/json 3.12.0、GoogleTest 1.17.0 和 spdlog v1.15.1；cpp-httplib 使用仓库中固定的提交。

本版本不包含数据库/MySQL、Redis、Docker、Kubernetes、前端、用户系统、认证/JWT、HTTPS/TLS、WebSocket、文件上传、微服务拆分或配置中心。

## 构建与测试

在项目根目录执行：

```powershell
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

Visual Studio 或其他 CMake 生成器可省略 `-G`，测试命令保持不变。HTTP 可执行目标名为 `DevManagerHttpServer`，输出文件为 `devmanager_http`（Windows 下为 `devmanager_http.exe`）。

本地 OpenAPI 检查（只使用 Python 标准库的契约检查）：

```powershell
python scripts/validate_openapi_contract.py
python scripts/validate_release_contract.py
```

完整 OpenAPI schema 检查使用固定版本：

```powershell
python -m pip install -r scripts/requirements-openapi.txt
python -m openapi_spec_validator docs/openapi.yaml
```

如果本机网络无法让 CMake 下载依赖，可准备 nlohmann/json 源码目录并传入 `-DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON="<目录>"`。

## 运行 CLI

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

删除确认只接受 `y/Y`；其他输入都会取消删除。新增或编辑项目时，技术栈标签以英文逗号分隔，例如 `C++, CMake, Linux Socket`。CLI 和 HTTP 服务不要同时写入同一个 JSON 文件。

## 运行 HTTP 服务

```powershell
.\build\devmanager_http.exe
```

服务默认监听 `127.0.0.1:8080`，也可以通过 `config/devmanager.json` 修改。接口如下：

| 方法 | 路径 | 说明 |
| --- | --- | --- |
| `GET` | `/api/projects` | 列出、搜索、筛选或排序项目 |
| `POST` | `/api/projects` | 创建项目 |
| `PUT` | `/api/projects/{id}` | 更新名称、技术栈、描述和状态，保留 ID |
| `DELETE` | `/api/projects/{id}` | 删除项目 |
| `GET` | `/health` | 检查 HTTP 进程是否运行 |
| `GET` | `/api/info` | 返回服务名称和 CMake 生成的版本 |
| `GET` | `/api/statistics` | 返回项目总数、状态统计和技术栈统计 |

`GET /api/projects` 支持 `name`、`technology`、`status` 三选一筛选，以及 `sort=id|name|status` 升序排序。筛选和排序同时提供时先筛选再排序。请求体使用 `Content-Type: application/json`，项目字段示例：

```json
{
  "name": "DevManager",
  "techStack": ["C++", "CMake"],
  "description": "local project manager",
  "status": "active"
}
```

每个 HTTP 响应都会带有 `X-Request-ID`。客户端提供符合 `^[A-Za-z0-9._-]+$` 且长度为 1 至 64 的值时，服务沿用该值；否则生成新的值。详细请求/响应字段、错误码和状态码以 [docs/openapi.yaml](docs/openapi.yaml) 为准。

## 数据文件

默认数据文件为 `data/projects.json`。文件不存在时以空项目库启动；新增或删除后创建/更新文件。读取到损坏或语义无效的快照时，程序报告错误，不覆盖原文件。删除后的项目 ID 永不复用。

## 项目结构

```text
CLI:  MenuController -> ProjectManager -> ProjectRepository -> JsonProjectRepository
HTTP: HttpServer -> ProjectHttpController -> ProjectService -> ProjectManager -> ProjectRepository
```

Bootstrap 负责组装配置、日志、仓储、业务对象和 HTTP 服务生命周期；Domain 和 ProjectManager 不依赖 HTTP、spdlog 或配置文件格式。
