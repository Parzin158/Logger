#include "Logger.h"
#include <iostream>

int main() {
    // 初始化Logger
    if (!Logger::init("logfile", true, 10 * 1024 * 1024)) {
        std::cerr << "Failed to initialize logger" << std::endl;
        return 1;
    }

    // 设置日志级别
    Logger::setLevel(INFO);

    // 输出不同级别的日志
    LOGI("This is an info message");
    LOGD("This is a debug message");
    LOGW("This is a warning message");
    LOGE("This is an error message");
    LOGSYSE("This is a system error message");
    LOGC("This is a critical message");

    // 使用变参函数传递参数
    int value = 42;
    LOGI("The value is %d", value);

    // 输出包含源文件名和行号的日志
    Logger::outPut(INFO, __FILE__, __LINE__, "This is an info message with file and line number");
    Logger::outPut(ERROR, __FILE__, __LINE__, "This is an error message with file and line number");

    // 输出二进制数据日志
    unsigned char binaryData[] = { 0xDE, 0xAD, 0xBE, 0xEF };
    Logger::outPutBinary(binaryData, sizeof(binaryData));

    // 关闭Logger
    Logger::uninit();

    return 0;
}
