/*
	异步日志：静态成员可以在不创建类实例的情况下访问，它允许从程序的任何地方写入日志。
*/
#ifndef __LOGGER__
#define __LOGGER__

#include <atomic>
#include <stdio.h>
#include <stdint.h>
#include <string>
#include <list>
#include <thread>
#include <mutex>
#include <condition_variable>

//#ifdef LOG_EXPORTS
//#define LOG_API __declspec(dllexport)
//#else
//#define LOG_API __declspec(dllimport)
//#endif

#define LOG_API

enum LOG_LEVEL
{
	TRACE,		// 最详细
	DEBUG,		// 开发调式
	INFO,		// 跟踪应用流程
	WARN,		// 异常和意外
	ERROR,		// 业务错误
	SYSERROR,	// 技术框架错误
	FATAL,		// 不可描述的程序崩溃
	CRITICAL,	// 关键信息
};


/*
如果打印的日志信息中有中文，则格式化字符串要用_T()宏包裹起来，
e.g.LOGI(_T("GroupID=%u, GroupName=%s, GroupName=%s."),
			lpGroupInfo->m_nGroupCode, lpGroupInfo->m_strAccount.c_str(), lpGroupInfo->m_strName.c_str());
*/

#define LOGT(...)	 Logger::outPut(TRACE, __FILE__, __LINE__, __VA_ARGS__)
#define LOGD(...)	 Logger::outPut(DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define LOGI(...)	 Logger::outPut(INFO, __FILE__, __LINE__, __VA_ARGS__)
#define LOGW(...)	 Logger::outPut(WARN, __FILE__, __LINE__, __VA_ARGS__)
#define LOGE(...)	 Logger::outPut(ERROR, __FILE__, __LINE__, __VA_ARGS__)
#define LOGSYSE(...) Logger::outPut(SYSERROR, __FILE__, __LINE__, __VA_ARGS__)
#define LOGF(...)	 Logger::outPut(FATAL, __FILE__, __LINE__, __VA_ARGS__)
#define LOGC(...)    Logger::outPut(CRITICAL, __FILE__, __LINE__, __VA_ARGS__)
#define LOGBINARY(buf, bufSize) Logger::outPutBinary(unsigned char* buffer, size_t bufSize)

class LOG_API Logger
{
public:
	static bool init(const char* fileName = nullptr,
		bool cutLongLine = false,
		int64_t rollSize = 10 * 1024 * 1024);
	static void uninit();
	static void setLevel(LOG_LEVEL level);
	static bool running();

	// 不输出线程ID号和所在函数签名、行号
	static bool outPut(long level, const char* pszFmt, ...);
	// 输出线程ID号和所在函数签名、行号	
	static bool outPut(long level,
		const char* fileName,
		int lineNum,
		const char* pszFmt, ...);
	// 输出二进制的数据包
	static bool outPutBinary(unsigned char* buffer, size_t bufSize);

private:
	Logger() = delete;
	~Logger() = delete;

	Logger(const Logger& rhs) = delete;
	Logger& operator=(const Logger& rhs) = delete;

	// 添加日志行前缀
	static void makeLinePrefix(long level, std::string& prefix);
	static void getTime(char* pszTime, int nTimeStrLength);
	static char*getPID(char* pid);
	static void debugStr(const std::string& str);
	static bool createDir(const char* dir);
	static bool createFile(const char* fileName);
	static bool writeToFile(const std::string& data);

	// 让程序主动崩溃
	static void crash();

	// 整数格式化填充为6位字符串
	static const char* ullto4Str(int n);

	// 格式化二进制日志数据为十六进制
	static char* formLog(int& index,				/*日志条目索引*/
		char* hexBuf,				/*存储格式化后的字符串*/
		size_t hexSize,
		unsigned char* binBuf,		/*待格式化的二进制数据*/
		size_t binSize);

	static void writeThreadProc();

private:
	static bool		                        m_ToFile;			// 日志写入文件还是写到控制台  
	static std::unique_ptr<std::ofstream>	m_LogFile;			// 日志文件
	static std::string						m_LogDir;			// 日志文件夹
	static std::string                      m_FileName;         // 日志文件名
	static std::string                      m_FileNamePID;		// 文件名中的进程id
	static bool                             m_TruncLongLog;     // 长日志是否截断
	static LOG_LEVEL                        m_Level;			// 当前日志级别
	static int64_t                          m_FileRollSize;     // 单个日志文件的最大字节数
	static int64_t                          m_WrittenSize;		// 已经写入的字节数目
	static std::list<std::string>           m_LinesToWrite;     // 待写入的日志
	static std::unique_ptr<std::thread>     m_WriteThread;
	static std::mutex                       m_WriteMtx;
	static std::condition_variable          m_WriteCond;
	static bool                             m_Exit;             // 退出标志
	static std::atomic<bool>                m_Running;          // 运行标志
};

#endif // !__LOGGER__
