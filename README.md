# DevManager

DevManager 是一个使用 C++17 和 CMake 构建的本地项目管理工具，提供 CLI 和 HTTP/JSON API。当前发布版本为 **v0.5.0**。

## v0.5.0 能力

- CLI：列出、新增、编辑、删除、名称/技术栈搜索、状态筛选和按 ID/名称/状态升序排序。
- HTTP：项目 CRUD、查询/筛选/排序，以及 `/health`、`/api/info` 和 `/api/statistics`。
- 存储：JSON 和 SQLite 是互斥、独立的后端；二者都提供相同的查询、排序和分页语义。
- 配置：缺失 `config/devmanager.json` 时仍使用 JSON 后端默认值；示例配置见 [`config/devmanager.example.json`](config/devmanager.example.json)。
- 请求追踪：HTTP 响应带有 `X-Request-ID`；详细契约以 [`docs/openapi.yaml`](docs/openapi.yaml) 为准。

版本只有一个来源：CMake 的 `project(DevManager VERSION 0.5.0 LANGUAGES C CXX)`。构建时由 CMake 生成 `DevManagerVersion.h`；运行时 `/api/info` 和测试读取生成值，代码中不手写版本号。

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
cmake -S . -B build-v05-final -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug
cmake --build build-v05-final --config Debug
ctest --test-dir build-v05-final -C Debug --output-on-failure
```

完整 CTest 可使用 `ctest --test-dir build-v05-final -C Debug --output-on-failure --timeout 60`。Visual Studio 或其他 CMake 生成器可省略 `-G`。依赖通过带固定 hash/提交的 CMake FetchContent 获取；网络不可用时只能复用已缓存依赖，不能关闭 TLS 或移除 hash 校验。

OpenAPI 检查：

```powershell
py -3 scripts/validate_openapi_contract.py
py -3 -m openapi_spec_validator docs/openapi.yaml
py -3 scripts/validate_release_contract.py
```

## 运行 CLI 和 HTTP 服务

```powershell
.\build-v05-final\DevManager.exe
.\build-v05-final\devmanager_http.exe
```

HTTP 服务默认监听 `127.0.0.1:8080`，也可通过 `config/devmanager.json` 修改。CLI 和 HTTP 服务不要同时写入同一个 JSON 文件。

接口包括：`GET/POST /api/projects`、`PUT/DELETE /api/projects/{id}`、`GET /health`、`GET /api/info` 和 `GET /api/statistics`。

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
