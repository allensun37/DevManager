# DevManager

DevManager 是一个使用 C++17 和 CMake 构建的本地项目管理工具，提供 CLI 和 HTTP/JSON API。当前发布版本为 **v0.7.0**。

## v0.7.0 能力

- CLI：列出、新增、编辑、删除、名称/技术栈搜索、状态筛选和按 ID/名称/状态升序排序。
- HTTP：项目 CRUD、查询/筛选/排序，以及公开的 `/health`、`/ready`、`/api/info` 和 `/api/statistics`。
- 存储：JSON 和 SQLite 是互斥、独立的后端；二者都提供相同的查询、排序和分页语义。
- 配置：缺失 `config/devmanager.json` 时仍使用 JSON 后端默认值；示例配置见 [`config/devmanager.example.json`](config/devmanager.example.json)。
- 请求追踪：HTTP 响应带有 `X-Request-ID`；请求体全局上限为 1 MiB，超出时返回 `413 payload_too_large`；详细契约以 [`docs/openapi.yaml`](docs/openapi.yaml) 为准。

版本只有一个来源：CMake 的 `project(DevManager VERSION 0.7.0 LANGUAGES C CXX)`。构建时由 CMake 生成 `DevManagerVersion.h`；运行时 `/api/info` 和测试读取生成值，代码中不手写版本号。

## HTTP API Key 认证（v0.6）

HTTP 服务使用单个本地 API Key。启动前设置 `DEVMANAGER_API_KEY` 环境变量；HTTP startup fails（环境变量缺失、为空或不可用时进程以非零状态退出，并且不会监听端口）。CLI 不读取此变量，仍可直接使用。

受保护的 HTTP 接口必须发送精确格式的 `Authorization: Bearer <API_KEY>` 请求头。认证失败统一返回 `401 unauthorized`、`WWW-Authenticate: Bearer` 和原有 JSON 错误结构；真实 API Key 不会写入响应或日志。

PowerShell 示例：

```powershell
$env:DEVMANAGER_API_KEY = "local-dev-key"
.\build-v07-final\devmanager_http.exe
```

请求示例：

```powershell
curl.exe -H "Authorization: Bearer local-dev-key" http://127.0.0.1:8080/api/projects
curl.exe http://127.0.0.1:8080/health
```

`/health` does not require an API key，便于健康探针访问。`/ready` does not require an API key；它只表示配置、Repository、SQLite migration、路由和认证初始化已经完成，并不检查数据库内容。Starting 或 Stopping 时 `/health` 仍为 200，而 `/ready` 返回 `503 not_ready`。v0.5 pagination and error contracts remain unchanged；分页参数和响应头继续遵循 [`docs/openapi.yaml`](docs/openapi.yaml)。请勿把真实 Key 写入源代码、README、日志或 Git 提交。

## 服务停止与请求体限制（v0.7）

HTTP 服务在收到 Windows Ctrl+C、Linux `SIGINT` 或 `SIGTERM` 后停止接受新连接，并等待已接收的请求完成。five-second drain deadline 只决定停止结果：五秒内完成时进程正常退出；超过期限会记录 `shutdown_timeout` 并以非零状态退出，但不会强杀或分离仍在运行的 C++ 线程。`/health` 仅表示 HTTP 进程存活，`/ready` 才表示服务就绪。

所有 HTTP 请求体共用 1 MiB 上限。当前业务请求体主要来自 POST/PUT；超过限制时会在进入业务层前返回 `413 payload_too_large`，并带有 `X-Request-ID`。

## 配置

程序从当前工作目录的 `config/devmanager.json` 读取配置。文件缺失时使用以下 JSON 默认配置（JSON 后端）：

```json
{
  "server": { "host": "127.0.0.1", "port": 8080 },
  "storage": { "type": "json", "path": "data/projects.json" },
  "logging": { "level": "info", "path": "logs/devmanager.log" }
}
```

可复制 [`config/devmanager.example.json`](config/devmanager.example.json) 作为 SQLite 配置起点：服务器为 `127.0.0.1:8080`，数据库为 `data/devmanager.db`，日志为 `logs/devmanager.log`。配置中的路径均相对于进程工作目录。

JSON 与 SQLite 后端互斥且彼此独立：一次运行只选择 `storage.type` 指定的一个后端。切换后端不会自动迁移数据，也不会删除原后端的数据；需要迁移时请先备份并自行转换。数据库 migration 只对 SQLite 后端执行，JSON 后端不会创建或更新 migration 表。

配置 JSON 损坏、根节点或字段类型不正确、空字符串以及端口不在 `1..65535` 范围内时，程序启动失败并返回非零状态。

## 构建、运行与测试

在项目根目录执行：

```powershell
cmake -S . -B build-v07-final -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug
cmake --build build-v07-final --config Debug
ctest --test-dir build-v07-final -C Debug --output-on-failure
```

完整 CTest 可使用 `ctest --test-dir build-v07-final -C Debug --output-on-failure --timeout 60`。Visual Studio 或其他 CMake 生成器可省略 `-G`。依赖通过带固定 hash/提交的 CMake FetchContent 获取；网络不可用时只能复用已缓存依赖，不能关闭 TLS 或移除 hash 校验。

OpenAPI 检查：

```powershell
py -3 scripts/validate_openapi_contract.py
py -3 -m openapi_spec_validator docs/openapi.yaml
py -3 scripts/validate_release_contract.py
```

## 运行 CLI 和 HTTP 服务

```powershell
.\build-v07-final\DevManager.exe
.\build-v07-final\devmanager_http.exe
```

HTTP 服务默认监听 `127.0.0.1:8080`，也可通过 `config/devmanager.json` 修改。CLI 和 HTTP 服务不要同时写入同一个 JSON 文件。

接口包括：`GET/POST /api/projects`、`PUT/DELETE /api/projects/{id}`、公开的 `GET /health`、`GET /ready`、`GET /api/info` 和 `GET /api/statistics`。

## JSON 后端

将配置设为：

```json
{
  "storage": { "type": "json", "path": "data/projects.json" }
}
```

文件不存在时以空项目库启动，写入时进行快照校验和安全替换；损坏或语义无效的快照不会覆盖原文件。

## SQLite 后端

将配置设为：

```json
{
  "storage": { "type": "sqlite", "path": "data/devmanager.db" }
}
```

首次打开 SQLite 数据库时自动运行内置 migration；migration 仅作用于 SQLite。SQLite 数据库和 JSON 快照是两份独立数据，切换 `storage.type` 不会迁移或删除任何一方。

## 查询与分页兼容性

`GET /api/projects` 支持 `name`、`technology`、`status` 三选一筛选，以及 `sort=id|name|status` 升序排序。`page` 和 `size` 是可选的分页参数，分页结果仍是 JSON 数组，并通过 `X-Total-Count`、`X-Page`、`X-Page-Size` 响应头返回元数据；缺少其中一个时分别默认为 `page=1` 或 `size=20`。不提供 `page`/`size` 时保持 v0.4 的数组契约，不返回 envelope。

## 依赖与范围

- CMake 3.20 或更高版本，以及支持 C++17 的编译器（GCC/MinGW 或 MSVC）。
- 固定版本的 nlohmann/json、GoogleTest、spdlog、cpp-httplib 和 SQLite amalgamation。
- 本版本不包含 MySQL、Redis、Docker、前端、用户系统或认证功能。
