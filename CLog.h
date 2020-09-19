#pragma once
#include "stdafx.h"


#define MAX_FILE_NAME 32
#define MAX_LOG_LENGTH 128
#define MAX_FILE_PATH 128



class SYSLOGCLASS
{
public:
	enum LOG_LEVEL
	{
		e_CONSOLE = 1,
		e_FILE = 2,
		e_DEBUG = 101,
		e_WARNNING = 102,
		e_ERROR = 103,
	};

private:
	FILE* _hFile;
	WCHAR _wFilePath[MAX_FILE_PATH];
	WCHAR _wFilePathSub[MAX_FILE_PATH];
	int _eDefaultLevel;
	int _bWriteFlag;

	DWORD _dw64LogCount;

	SRWLOCK _SRWLOCK;

	static SYSLOGCLASS* _pSysLog;

	SYSLOGCLASS()
	{
		_hFile = nullptr;
		_wFilePath[0] = { '\0' };
		_wFilePathSub[0] = { '\0' };
		_dw64LogCount = 0;
		_eDefaultLevel = e_DEBUG;
		InitializeSRWLock(&_SRWLOCK);
		_wmkdir(L"_LOG");
	}

	~SYSLOGCLASS()
	{

	}

public:

	// 기본 폴더(_LOG) 설정
	void SET_LOG(int iWriteFlag, int iLogLevel)
	{
		_bWriteFlag = iWriteFlag;
		_eDefaultLevel = iLogLevel;
		wsprintf(_wFilePathSub, L"_LOG");
	}

	void SET_LOG(BOOL writeFlag, LOG_LEVEL logLevel, WCHAR* szDir_SubName)
	{
		_eDefaultLevel = logLevel;
		_bWriteFlag = writeFlag;
		StringCchPrintf(_wFilePathSub, MAX_FILE_PATH,L"_LOG\\%s", szDir_SubName);
	}

	void LOG_SET_DIR(const WCHAR* szDir)
	{
		wsprintf(_wFilePath, L"%s\\%s", _wFilePathSub, szDir);
		_wmkdir(_wFilePath);
	}

	void SYSLOG_LEVEL(LOG_LEVEL level)
	{
		_eDefaultLevel = level;
	}

	void LOG(const WCHAR* szType, LOG_LEVEL level,const WCHAR* szStringFormat,...)
	{
		if (level < _eDefaultLevel)
			return;

		InterlockedIncrement(&_dw64LogCount);

		WCHAR szLogLevel[64];

		switch (level)
		{
		case LOG_LEVEL::e_DEBUG:
			StringCchPrintf(szLogLevel, 64, L"DEBUG");
			break;

		case LOG_LEVEL::e_ERROR:
			StringCchPrintf(szLogLevel, 64, L"ERROR");
			break;

		case LOG_LEVEL::e_WARNNING:
			StringCchPrintf(szLogLevel, 64, L"WARNNING");
			break;
		}

		HRESULT hResult;
		va_list va;
		WCHAR szInMessage[1024]{ '\0' };
		va_start(va, szStringFormat);
		hResult  = StringCchVPrintf(szInMessage, 256, szStringFormat, va);
		va_end(va);

		if (FAILED(hResult))
		{
			szInMessage[1023] = '\0';
			LOG(szType, LOG_LEVEL::e_WARNNING, L"TOO long");
		}

		SYSTEMTIME stSysTime;
		GetLocalTime(&stSysTime);

		// WriteFlag를 확인하여 flag에 해당하는 것만 출력
		if (_bWriteFlag & e_CONSOLE)
		{
			// 콘솔에 로그 출력
			wprintf(L"[%s] [%02d:%02d:%02d / %s] %s\n", szType, stSysTime.wHour, stSysTime.wMinute, stSysTime.wSecond, szLogLevel, szInMessage);
		}

		if (_bWriteFlag & e_FILE)
		{
			// 파일에 로그 저장
			LOG_SET_DIR(szType);

			WCHAR szFileName[MAX_FILE_NAME];
			StringCchPrintf(szFileName, MAX_FILE_NAME, L"%s\\%s_%d%02d%02d.txt", _wFilePath, szType, stSysTime.wYear, stSysTime.wMonth, stSysTime.wDay);

			// 로그
			WCHAR szLog[2048] = { 0, };
			StringCchPrintf(szLog, 2048,L"[%s] [%d-%02d-%02d %02d:%02d:%02d / %s / %08d] %s\n", szType, stSysTime.wYear, stSysTime.wMonth, stSysTime.wDay,
				stSysTime.wHour, stSysTime.wMinute, stSysTime.wSecond, szLogLevel, _dw64LogCount, szInMessage);


			AcquireSRWLockExclusive(&_SRWLOCK);
			FILE* fLog;
			do
			{
				_wfopen_s(&fLog, szFileName, L"a");
			} while (fLog == NULL);

			fputws(szLog, fLog);
			fclose(fLog);
			ReleaseSRWLockExclusive(&_SRWLOCK);
		}

	}

	static SYSLOGCLASS* GetInstance(void)
	{

		static SYSLOGCLASS _pSysLog;
		return &_pSysLog;
	}

	static void Destroy()
	{
		delete _pSysLog;
	}
};

#define LOG		SYSLOGCLASS::GetInstance()->LOG
#define LOG_SET SYSLOGCLASS::GetInstance()->SET_LOG

#define LOG_CONSOLE SYSLOGCLASS::e_CONSOLE
#define LOG_FILE	SYSLOGCLASS::e_FILE
#define LOG_DEBUG	SYSLOGCLASS::e_DEBUG
#define LOG_WARNNING	SYSLOGCLASS::e_WARNNING
#define LOG_ERROR	SYSLOGCLASS::e_ERROR
