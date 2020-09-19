#include "stdafx.h"

#include "CNewRingBuffer.h"


RingBuffer::RingBuffer()
{
	_buffer = NULL;
	_bufferSize = 0;

	_readPos = 0;
	_writePos = 0;
	Initial(DEFAULT_SIZE);

}
RingBuffer::RingBuffer(int iBufferSize)
{
	_buffer = NULL;
	_bufferSize = 0;

	_readPos = 0;
	_writePos = 0;

	Initial(iBufferSize);
}
RingBuffer::~RingBuffer()
{
	if (NULL != _buffer)
		delete[] _buffer;

	_buffer = NULL;
	_bufferSize = 0;

	_readPos = 0;
	_writePos = 0;

	wprintf(L"Here\n");

}

void RingBuffer::Initial(int iBufferSize)
{
	if (NULL != _buffer)
		delete[] _buffer;

	if (0 >= iBufferSize) return;

	_bufferSize = iBufferSize;

	_readPos = 0;
	_writePos = 0;

	_buffer = new char[iBufferSize];

}


int	RingBuffer::GetBufferSize(void)
{
	if (NULL != _buffer)
	{
		return _bufferSize - BLANK_SIZE;
	}

	return 0;
}

int RingBuffer::GetUseSize(void)
{
	if (_readPos <= _writePos)
	{
		return _writePos - _readPos;
	}
	else
	{
		return _bufferSize - _readPos + _writePos;
	}

}

int RingBuffer::GetFreeSize(void)
{
	return _bufferSize - (GetUseSize() + BLANK_SIZE);
}

int RingBuffer::GetNotBrokenGetSize(void)
{
	if (_readPos <= _writePos)
	{
		return _writePos - _readPos;
	}
	else
	{
		return _bufferSize - _readPos;
	}
}

int RingBuffer::GetNotBrokenPutSize(void)
{
	if (_writePos < _readPos)
	{
		return (_readPos - _writePos) - BLANK_SIZE;
	}
	else
	{
		if (_readPos < BLANK_SIZE)
		{
			return (_bufferSize - _writePos) - (BLANK_SIZE - _readPos);
		}
		else
		{
			return _bufferSize - _writePos;
		}
	}
}

int RingBuffer::Put(char* chpData, int iSize)
{
	int iWrite;

	if (GetFreeSize() < iSize)
	{
		return 0;
		//	iSize = GetFreeSize();
	}

	if (0 >= iSize)
		return 0;

	if (_readPos <= _writePos)
	{
		iWrite = _bufferSize - _writePos;

		if (iWrite >= iSize)
		{
			memcpy(_buffer + _writePos, chpData, iSize);
			_writePos += iSize;
		}
		else
		{
			memcpy(_buffer + _writePos, chpData, iWrite);
			memcpy(_buffer, chpData + iWrite, iSize - iWrite);
			_writePos = iSize - iWrite;
		}
	}
	else
	{
		memcpy(_buffer + _writePos, chpData, iSize);
		_writePos += iSize;
	}

	_writePos = _writePos == _bufferSize ? 0 : _writePos;

	return iSize;
}
int RingBuffer::Get(char* chpDest, int iSize)
{
	int iRead;

	int realsize = GetUseSize();

	if (realsize < iSize)
		iSize = realsize;

	if (0 >= iSize)
		return 0;

	if (_readPos <= _writePos)
	{
		memcpy(chpDest, _buffer + _readPos, iSize);
		_readPos += iSize;
	}
	else
	{
		iRead = _bufferSize - _readPos;

		if (iRead >= iSize)
		{
			memcpy(chpDest, _buffer + _readPos, iSize);
			_readPos += iSize;
		}
		else
		{
			memcpy(chpDest, _buffer + _readPos, iRead);
			memcpy(chpDest + iRead, _buffer, iSize - iRead);
			_readPos = iSize - iRead;
		}
	}

	return iSize;
}
int	RingBuffer::Peek(char* chpDest, int iSize)
{
	int iRead;

	int realsize = GetUseSize();

	if (realsize < iSize)
		iSize = realsize;

	if (0 >= iSize)
		return 0;

	if (_readPos <= _writePos)
	{
		memcpy(chpDest, _buffer + _readPos, iSize);
	}
	else
	{
		iRead = _bufferSize - _readPos;
		if (iRead >= iSize)
		{
			memcpy(chpDest, _buffer + _readPos, iSize);
		}
		else
		{
			memcpy(chpDest, _buffer + _readPos, iRead);
			memcpy(chpDest + iRead, _buffer, iSize - iRead);
		}
	}

	return iSize;

}

void RingBuffer::MoveWritePos(int iSize)
{
	_writePos = (_writePos + iSize) % _bufferSize;
}

void RingBuffer::RemoveData(int iSize)
{
	int iRead;

	if (GetUseSize() < iSize)
		iSize = GetUseSize();

	if (0 >= iSize)
		return;

	if (_readPos < _writePos)
	{
		_readPos += iSize;
	}
	else
	{
		iRead = _bufferSize - _readPos;

		if (iRead >= iSize)
		{
			_readPos += iSize;
		}
		else
		{
			_readPos = iSize - iRead;
		}
	}

	_readPos = _readPos == _bufferSize ? 0 : _readPos;
}

void RingBuffer::ClearBuffer(void)
{
	_readPos = 0;
	_writePos = 0;
}


char* RingBuffer::GetBufferPtr(void)
{
	return _buffer;
}

char* RingBuffer::GetReadBufferPtr(void)
{
	return _buffer + _readPos;
}

char* RingBuffer::GetWriteBufferPtr(void)
{
	return _buffer + _writePos;
}