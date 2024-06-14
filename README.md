# Logger

一个高度灵活且高效的异步日志库，适用于 C++ 应用程序，该库支持将日志记录到文件和控制台。
- 支持 C++11 或更高版本的 C++ 编译器

## 功能

- 异步日志记录
- 支持多种日志级别：
  - TRACE
  - DEBUG
  - INFO
  - WARN
  - ERROR
  - SYSERROR
  - FATAL
  - CRITICAL
- 基于文件大小的日志文件滚动
- 长日志消息截断
- 记录二进制数据
- 线程安全

## 实现逻辑

1. `init()`初始化并启动日志，设置文件名、是否截断长日志、设置单个日志文件最大字节数
2. 根据`fileName`，在工作路径创建Log文件夹
3. `m_WriteThread.reset()`开启工作线程，异步方式执行`writeThreadProc`函数进行写日志操作
4. 在工作线程中，当日志启动首次或日志文件超过`m_FileRollSize`在工作路径/Log文件夹中创建新日志文件。
