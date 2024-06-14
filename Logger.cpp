#include "Logger.h"
#include <iostream>
#include <fstream>
#include <limits.h>
#include <string.h>
#include <stdarg.h>
#include <sstream>

#ifdef _WIN32
#include <Windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif 


#define MAX_LINE_LENGTH   256
#define DEFAULT_ROLL_SIZE 10 * 1024 * 1024

bool Logger::m_TruncLongLog = false; // 默认不截断长日志
std::unique_ptr<std::ofstream> Logger::m_LogFile;
std::string Logger::m_LogDir = "";
std::string Logger::m_FileName = "default";
std::string Logger::m_FileNamePID = "";
LOG_LEVEL Logger::m_Level = INFO;
int64_t Logger::m_FileRollSize = DEFAULT_ROLL_SIZE;
int64_t Logger::m_WrittenSize = 0;
std::list<std::string> Logger::m_LinesToWrite;
std::unique_ptr<std::thread> Logger::m_WriteThread;
std::mutex Logger::m_WriteMtx;
std::condition_variable Logger::m_WriteCond;
bool Logger::Logger::m_Exit = false;
std::atomic<bool> Logger::m_Running(false);

bool Logger::init(const char* fileName, bool cutLongLine, int64_t rollSize)
{
	m_TruncLongLog = cutLongLine;
	m_FileRollSize = rollSize;

	// 文件名安全检查--fileName可能是字符指针或字符数组
	if (fileName == nullptr || fileName[0] == 0) {
		m_FileName.clear();
	}
	else {
		m_FileName = fileName;
	}

	char pid[8];
	m_FileNamePID.assign(getPID(pid));

	// 创建日志文件夹
#ifdef _WIN32
	if (!createDir("//Log")) return false;
#else
	if (!createDir("/Log")) return false;
#endif // _WIN32	

	// 创建新线程写日志
	m_WriteThread.reset(new std::thread(writeThreadProc)); 

	return true;
}

void Logger::uninit()
{
	m_Exit = true;

	m_WriteCond.notify_one();

	if (m_WriteThread->joinable())
		m_WriteThread->join();

	if (m_LogFile != nullptr) {
		m_LogFile->close();
		m_LogFile = nullptr;
	}
}

void Logger::setLevel(LOG_LEVEL level)
{
	if (level < TRACE || level > CRITICAL)
		return;

	m_Level = level;
}

bool Logger::running()
{
	return m_Running;
}

bool Logger::outPut(long level, const char* pszFmt, ...)
{
	// 不记录源文件和行号信息
	if (level != CRITICAL && level < m_Level)
		return false;

	std::string strLine;
	// 日志行前缀
	makeLinePrefix(level, strLine);

	// log正文
	std::string logMsg;

	// 先计算不定参数的长度，便于分配空间
	va_list argsCount;
	va_start(argsCount, pszFmt);
	int logMsgLength = vsnprintf(NULL, 0, pszFmt, argsCount);
	va_end(argsCount);

	if ((int)logMsg.capacity() < logMsgLength + 1)
		logMsg.resize(logMsgLength + 1);

	va_list args;
	va_start(args, pszFmt);
	vsnprintf(&logMsg[0], logMsg.capacity(), pszFmt, args);
	va_end(args);

	logMsg.resize(logMsgLength + 1);

	// 日志开启截断，长日志只保留MAX_LINE_LENGTH个字符
	if (m_TruncLongLog)
		logMsg = logMsg.substr(0, MAX_LINE_LENGTH);

	strLine += logMsg;

	if (!m_FileName.empty())
		strLine += "\n";

	// 写log
	if (level != FATAL) {
		std::lock_guard<std::mutex> lock_guard(m_WriteMtx);
		m_LinesToWrite.push_back(strLine);
		m_WriteCond.notify_one();
	}
	else {
		// 同步写日志，使FATAL级日志能立即crash程序
		debugStr(strLine);

		if (!m_FileName.empty()) { // 文件名非空 
			if (m_LogFile == nullptr) { // 日志文件未创建
				if (!createFile(m_FileName.c_str()))
					return false;
			}
			writeToFile(strLine);
		}
		crash();
	}
	return true;
}

bool Logger::outPut(long level, const char* fileName, int lineNum, const char* pszFmt, ...)
{
	if (level != CRITICAL && level < m_Level)
		return false;

	std::string strLine;
	makeLinePrefix(level, strLine);

	// 函数签名
	char sigMsg[512] = { 0 };
	snprintf(sigMsg, sizeof(sigMsg), "[%s:%d]:", fileName, lineNum);
	strLine += sigMsg;

	// 日志正文
	std::string logMsg;

	// 先计算不定参数的长度，便于分配空间
	va_list argsCount;
	va_start(argsCount, pszFmt);
	int logMsgLength = vsnprintf(NULL, 0, pszFmt, argsCount);
	va_end(argsCount);

	if ((int)logMsg.capacity() < logMsgLength + 1)
		logMsg.resize(logMsgLength + 1);

	va_list args;
	va_start(args, pszFmt);
	vsnprintf(&logMsg[0], logMsg.capacity(), pszFmt, args);
	va_end(args);

	logMsg.resize(logMsgLength + 1);

	// 日志开启截断，长日志只保留MAX_LINE_LENGTH个字符
	if (m_TruncLongLog)
		logMsg = logMsg.substr(0, MAX_LINE_LENGTH);

	strLine += logMsg;

	if (!m_FileName.empty()) // 非输出控制台，在每行末加换行符
		strLine += "\n";

	// 写log
	if (level != FATAL) {
		std::lock_guard<std::mutex> lock_guard(m_WriteMtx);
		m_LinesToWrite.push_back(strLine);
		m_WriteCond.notify_one();
	}
	else {
		// 同步写日志，使FATAL级日志能立即crash程序

		debugStr(strLine);

		if (!m_FileName.empty()) { // 文件名非空 
			if (m_LogFile == nullptr) { // 日志文件未创建
				if (!createFile(m_FileName.c_str()))
					return false;
			}
			writeToFile(strLine);
		}
		crash(); // 主动crash程序
	}

	return true;
}

bool Logger::outPutBinary(unsigned char* buffer, size_t bufSize)
{
	// address[140721591726995] size[5] 

	std::ostringstream ostr;

	static const size_t PRTMAXSIZE = 512; // 每次处理最大512字节
	char buf[PRTMAXSIZE * 3 + 8];

	size_t doneSize = 0;
	size_t prtBufSize = 0;
	int index = 0;
	ostr << "address[" << (long)buffer << "] size[" << bufSize << "] \n";

	while (true) {
		memset(buf, 0, sizeof(buf));
		if (bufSize > doneSize) {
			prtBufSize = (bufSize - doneSize); // 要处理的字节数
			if (prtBufSize > PRTMAXSIZE)
				prtBufSize = PRTMAXSIZE;

			// 格式化日志
			formLog(index, buf, sizeof(buf), buffer + doneSize, prtBufSize);

			ostr << buf;
			doneSize += prtBufSize;
		}
		else // 已处理字节数 >= bufsize
			break;
	} // end while

	std::lock_guard<std::mutex> lock_guard(m_WriteMtx);
	m_LinesToWrite.push_back(ostr.str());
	m_WriteCond.notify_one();

	return true;
}

void Logger::makeLinePrefix(long level, std::string& prefix)
{
	// [INFO][TRACE][2023-05-04 14:30:45][140104013296576]

	// 日志级别
	prefix = "[INFO]";
	if (level == TRACE)
		prefix = "[TRACE]";
	else if (level == DEBUG)
		prefix = "[DEBUG]";
	else if (level == WARN)
		prefix = "[WARN]";
	else if (level == ERROR)
		prefix = "[ERROR]";
	else if (level == SYSERROR)
		prefix = "[SYSE]";
	else if (level == FATAL)
		prefix = "[FATAL]";
	else if (level == CRITICAL)
		prefix = "[CRITICAL]";

	//时间
	char szTime[64] = { 0 };
	getTime(szTime, sizeof(szTime));

	prefix += "[";
	prefix += szTime;
	prefix += "]";

	//当前线程信息
	char threadID[32] = { 0 };
	std::ostringstream osThreadID;
	osThreadID << std::this_thread::get_id();
	snprintf(threadID, sizeof(threadID), "[%s]", osThreadID.str().c_str());
	prefix += threadID;
}

void Logger::getTime(char* pszTime, int timeStrLength)
{
	memset(pszTime, 0, timeStrLength);
	std::time_t now = std::time(NULL);
	std::tm time;

#ifdef _WIN32
	localtime_s(&time, &now);
#else
	localtime_r(&now, &time);
#endif 
	strftime(pszTime, timeStrLength, "%Y%m%d%H%M%S", &time);
}

char* Logger::getPID(char* pid)
{
	memset(pid, 0, sizeof(pid));

#ifdef _WIN32
	snprintf(pid, sizeof(pid), "%05d", GetCurrentProcessId());
#else 
	snprintf(pid, sizeof(pid), "%05d", getpid());
#endif 

	return pid;
}

void Logger::debugStr(const std::string& str)
{
	std::cout << str << std::endl;

#ifdef _WIN32
	OutputDebugStringA(strLine.c_str());
	OutputDebugStringA("\n");
#endif 
}

bool Logger::createDir(const char* dir)
{
	// 获取工作路径
	char logDir[PATH_MAX] = { 0 };

#ifdef _WIN32
	if (GetCurrentDirectory(MAX_PATH, logDir) == 0) {
		perror("GetCurrentDirectory");
		return false;
	}
#else
	if (getcwd(logDir, sizeof(logDir)) == NULL) {
		perror("getcwd");
		return false;
	}
#endif 

	if (strlen(logDir) + sizeof(dir) >= PATH_MAX)
		return false;

	strcat(logDir, dir);

	if (!m_LogDir.empty())
		m_LogDir = nullptr;

	m_LogDir.append(logDir);

#ifdef _WIN32
	DWORD att = GetFileAttributes(logDir); // 获取logDir目录信息
	if (att == INVALID_FILE_ATTRIBUTES) {
		std::cerr << "Failed to get file attributes. Error: " << GetLastError() << std::endl;
		return false;
	}

	if (!(att & FILE_ATTRIBUTE_DIRECTORY)) {
		// 目录不存在
		if (CreateDirectory(m_LogDir, NULL) == 0) {
			perror("CreatDirectory");
			return false;
		}
	}

#else
	struct stat dirInfo; // 获取目录信息
	int ret = stat(logDir, &dirInfo);

	// 目录不存在时创建日志目录
	if (ret == -1 && errno == ENOENT) {
		if (mkdir(logDir, 0775) == -1) {
			perror("mkdir");
			return false;
		}
	}
#endif 

	return true; // 文件夹存在
}

bool Logger::createFile(const char* fileName)
{
	// 创建文件
	char szNow[64] = { 0 };
	getTime(szNow, sizeof(szNow));

	std::string newFileName(m_LogDir);
#ifdef _WIN32
	newFileName += "//";
#endif 
	newFileName += "/";
	newFileName += fileName;
	newFileName += ".";
	newFileName += szNow;
	newFileName += ".";
	newFileName += m_FileNamePID;
	newFileName += ".log";

	std::ofstream* file = new std::ofstream(newFileName, std::ios::out | std::ios::trunc);
	if (!file) {
		perror("file");
		return false;
	}
	m_LogFile.reset(file);
	return true;
}

bool Logger::writeToFile(const std::string& data)
{
	std::lock_guard<std::mutex> lock(m_WriteMtx); 
	if (!m_LogFile || !m_LogFile->is_open()) {
		std::cerr << "File stream in a bad state." << std::endl;
		return false; // 文件流不存在或未打开报错
	}

	const std::string temp(data);
	m_LogFile->write(temp.c_str(), temp.size());
	if (m_LogFile->fail()) {
		std::cerr << "Log stream is in a bad state." << std::endl;
		return false;
	}

	m_LogFile->flush();
	return true;
}

void Logger::crash()
{
	char* p = nullptr;
	*p = 0;
}

const char* Logger::ullto4Str(int n)
{
	static char buf[64 + 1];
	memset(buf, 0, sizeof(buf));
	// 0填充至最高6位无符号整数格式
	snprintf(buf, sizeof(buf), "%06u", n);
	return buf;
}

char* Logger::formLog(int& index,					/*日志条目索引*/
	char* hexBuf,					/*存储格式化后的字符串*/
	size_t hexSize,
	unsigned char* binBuf,		/*待格式化的二进制数据*/
	size_t binSize)
{
	size_t doneHex = 0;
	size_t doneBin = 0;
	int headLen = 0;
	char szhead[64 + 1] = { 0 }; // 保存日志条目索引
	char szchar[17] = "0123456789abcdef";

	while (hexSize > doneBin && binSize > doneHex + 10) {
		if (doneBin % 32 == 0) { // 每32字节添加新行
			if (headLen != 0)
				hexBuf[doneHex++] = '\n';
			memset(szhead, 0, sizeof(szhead));
			strncpy(szhead, ullto4Str(index++), sizeof(szhead) - 1);
			headLen = strlen(szhead);
			szhead[headLen++] = ' ';

			strcat(hexBuf, szhead); // 日志条目索引添加到szBuf中
			doneHex += headLen;
		}
		if (doneBin % 16 == 0 && 0 != headLen) // 每16字节添加一个空格
			hexBuf[doneHex++] = ' ';
		// 二进制数据格式化为十六进制
		hexBuf[doneHex++] = szchar[(binBuf[doneBin] >> 4) & 0xf]; // 高4位
		hexBuf[doneHex++] = szchar[(binBuf[doneBin]) & 0xf];		// 低4位
		doneHex++;
	}
	hexBuf[doneHex++] = '\n';
	hexBuf[doneHex++] = '\0';
	return hexBuf;
}

void Logger::writeThreadProc()
{
	m_Running = true;
	
	while (true) {
		if (!m_FileName.empty()) {
			// 首次启动或文件大小超过m_FileRollSize，新建文件
			if (m_LogFile == nullptr || m_WrittenSize >= m_FileRollSize) {
				m_WrittenSize = 0; // 重置m_WrittenSize

				std::string strNewFileName(m_FileName);
				if (!createFile(strNewFileName.c_str())) return;
			}
		}

		std::string strLine;
		{
			std::unique_lock<std::mutex> guard(m_WriteMtx);
			while (m_LinesToWrite.empty()) {
				if (m_Exit)	return;
				m_WriteCond.wait(guard);
			}
			strLine = m_LinesToWrite.front();
			m_LinesToWrite.pop_front();
		}

		debugStr(strLine);

		if (!m_FileName.empty()) {
			if (!writeToFile(strLine))	return;
			m_WrittenSize += strLine.length();
		}
	} // end while-loop

	m_Running = false;
}
