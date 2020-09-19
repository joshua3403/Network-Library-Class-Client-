#pragma once
#include "stdafx.h"

class RingBuffer
{
public:
	enum
	{
		DEFAULT_SIZE = 40008,
		BLANK_SIZE = 8
	};

public:

	RingBuffer(void);
	RingBuffer(int iBufferSize);

	virtual	~RingBuffer(void);
	void Initial(int iBufferSize);
	int GetBufferSize(void);
	int GetUseSize(void);
	int GetFreeSize(void);
	int GetNotBrokenGetSize(void);
	int GetNotBrokenPutSize(void);
	int Put(char* chpData, int iSize);
	int Get(char* chpDest, int iSize);
	int Peek(char* chpDest, int iSize);


	void MoveWritePos(int iSize);
	void RemoveData(int iSize);
	void ClearBuffer(void);
	char* GetBufferPtr(void);
	char* GetReadBufferPtr(void);
	char* GetWriteBufferPtr(void);


protected:

	char* _buffer;
	int _bufferSize;
	int _readPos;
	int _writePos;
};