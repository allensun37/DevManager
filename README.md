# DevManager

一个使用 C++17 和 CMake 构建的命令行个人项目管理工具。

当前版本为 v0.1 开发骨架。后续将实现项目的新增、删除、名称搜索、技术栈搜索和 JSON 持久化。

## 构建

需要 CMake、支持 C++17 的编译器，以及网络访问以由 CMake 获取 nlohmann/json。

```powershell
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

## 运行

```powershell
.\build\DevManager.exe
```
