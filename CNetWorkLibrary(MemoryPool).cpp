#include "stdafx.h"
#include "CCrashDumpClass.h"
#include "CNetWorkLibrary(MemoryPool).h"

BOOL joshua::CNetworkLibraryClient::InitialNetwork(const WCHAR* ip, DWORD port, BOOL Nagle)
{
	int retval;
	bool fail = false;
	_bNagle = Nagle;
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
	{
		LOG(L"SERVER", LOG_ERROR, L"%s\n", L"WSAStartup() Error!");
		return fail;
	}

	// bind()
	ZeroMemory(&_serveraddr, sizeof(_serveraddr));
	_serveraddr.sin_family = AF_INET;
	if(ip == NULL)
		_serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
	else
		InetPton(AF_INET, ip, &_serveraddr.sin_addr);
	_serveraddr.sin_port = htons(port);

	return (!fail);
}

BOOL joshua::CNetworkLibraryClient::CreateThread(DWORD threadCount)
{
	SYSTEM_INFO si;
	GetSystemInfo(&si);
	HANDLE hThread = NULL;
	// ������ ����
	_iThreadCount = threadCount;
	if (_iThreadCount == NULL || _iThreadCount > si.dwNumberOfProcessors * 2)
	{
		for (int i = 0; i < si.dwNumberOfProcessors * 2; i++)
		{
			hThread = (HANDLE)_beginthreadex(NULL, 0, WorkerThread, (LPVOID)this, 0, NULL);
			if (hThread == NULL)
				return FALSE;
			CloseHandle(hThread);
			_ThreadVector.push_back(hThread);
		}
		_iThreadCount = si.dwNumberOfProcessors * 2;
	}
	else
	{
		for (int i = 0; i < _iThreadCount; i++)
		{
			hThread = (HANDLE)_beginthreadex(NULL, 0, WorkerThread, (LPVOID)this, 0, NULL);
			if (hThread == NULL)
				return FALSE;
			CloseHandle(hThread);
			_ThreadVector.push_back(hThread);
		}
	}
	return TRUE;
}

BOOL joshua::CNetworkLibraryClient::CreateSession()
{
	_SessionArray = new st_SESSION[_dwSessionMax];
	for(__int64 i = _dwSessionMax - 1; i >= 0; --i)
	{
		_SessionArray[i].index = i;
		PushIndex(i);
	}
	return TRUE;
}

void joshua::CNetworkLibraryClient::PushIndex(UINT64 index)
{
	EnterCriticalSection(&_IndexStackCS);
	_ArrayIndex.push(index);
	LeaveCriticalSection(&_IndexStackCS);
}

UINT64 joshua::CNetworkLibraryClient::PopIndex()
{
	EnterCriticalSection(&_IndexStackCS);
	UINT64 temp = _ArrayIndex.top();
	_ArrayIndex.pop();
	LeaveCriticalSection(&_IndexStackCS);

	return temp;
 
}

joshua::st_SESSION* joshua::CNetworkLibraryClient::InsertSession(SOCKET sock, SOCKADDR_IN* sockaddr)
{
	UINT64 temp = -1;
	temp = PopIndex();
	if (temp == -1)
	{
		LOG(L"SERVER", LOG_ERROR, L"IndexStack Lack. CurrentSesionCount : %d\n", _dwSessionCount);
		return nullptr;
	}
	else
	{
		// TCP ���Ͽ��� ���񼼼� ó��
		// ���� ������ ������������ ����(���� ����, Ȥ�� �ܺο��� ������ �Ϻη� ���� ���)�Ǿ� ������ Ŭ���̾�Ʈ����
		// ������ ������� �𸣴� ������ ������ �ǹ� (Close �̺�Ʈ�� ���� ����)
		// TCP KeepAlive���
				// - SO_KEEPALIVE : �ý��� ������Ʈ�� �� ����. �ý����� ��� SOCKET�� ���ؼ� KEEPALIVE ����
		// - SIO_KEEPALIVE_VALS : Ư�� SOCKET�� KEEPALIVE ����
		//tcp_keepalive tcpkl;
		//tcpkl.onoff = TRUE;
		//tcpkl.keepalivetime = 30000; // ms
		//tcpkl.keepaliveinterval = 1000;
		//WSAIoctl(sock, SIO_KEEPALIVE_VALS, &tcpkl, sizeof(tcp_keepalive), 0, 0, NULL, NULL, NULL);
	/*	setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char*)&_bNagle, sizeof(_bNagle));*/

		st_SESSION* pSession = &_SessionArray[temp];
		pSession->SessionID = CreateSessionID(++_dwSessionID, temp);
		pSession->socket = sock;
		pSession->bIsSend = FALSE;
		pSession->dwPacketCount = 0;
		pSession->SendBuffer.Clear();
		pSession->RecvBuffer.ClearBuffer();
		pSession->lIO->lIOCount = 0;
		pSession->lIO->bIsReleased = FALSE;
		InterlockedIncrement64(&pSession->lIO->lIOCount);
		memcpy(&pSession->clientaddr, sockaddr, sizeof(SOCKADDR_IN));
		InterlockedIncrement64(&_dwSessionCount);
		InterlockedIncrement64(&_dwCount);
		return pSession;
	}
}

//unsigned int __stdcall joshua::CNetworkLibraryClient::ConnectThread(LPVOID lpParam)
//{
//	((CNetworkLibraryClient*)lpParam)->ConnectThread();
//
//	return 0;
//}

unsigned int __stdcall joshua::CNetworkLibraryClient::WorkerThread(LPVOID lpParam)
{
	((CNetworkLibraryClient*)lpParam)->WorkerThread();
	return 0;
}

void joshua::CNetworkLibraryClient::CreateSocket()
{
	int retval = 0;
	for (int i = 0; i < _dwSessionMax; i++)
	{
		SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
		if (sock == INVALID_SOCKET)
		{
			wprintf(L"socket() eror\n");
			return;
		}

		// �ͺ�� �������� ��ȯ
		u_long on = 1;
		retval = ioctlsocket(sock, FIONBIO, &on);
		if (retval == SOCKET_ERROR)
		{
			wprintf(L"ioctlsocket() eror\n");
			return;
		}

		// ���̱� �˰��� ����
		BOOL optval = TRUE;
		setsockopt(sock, IPPROTO_IP, TCP_NODELAY, (char*)&optval, sizeof(optval));

		_SocketVector.push_back(sock);
	}
}

void joshua::CNetworkLibraryClient::ConnectToServer()
{
	FD_SET writeSet, errorSet;
	SOCKET UserTable_Socket[FD_SETSIZE];
	int addrlen, retval;
	int iSessionCount = 0;

	FD_ZERO(&errorSet);
	FD_ZERO(&writeSet);
	memset(UserTable_Socket, INVALID_SOCKET, sizeof(SOCKET) * FD_SETSIZE);

	for (int i = 0; i < _SocketVector.size(); i++)
	{
		retval = connect(_SocketVector[i], (SOCKADDR*)&_serveraddr, sizeof(SOCKADDR));

		UserTable_Socket[iSessionCount] = _SocketVector[i];
		FD_SET(_SocketVector[i], &writeSet);
		_dwSessionCount++;

		if (retval == SOCKET_ERROR)
		{
			DWORD error = WSAGetLastError();

			if (error == WSAEWOULDBLOCK)
				continue;
			else
			{
				wprintf(L"Connect() error socket : %d, Error Code : %d\n", _SocketVector[i], error);
			}
		}
		//}
		if (iSessionCount >= FD_SETSIZE)
		{
			SelectConnect(UserTable_Socket, &writeSet, &errorSet);

			FD_ZERO(&errorSet);
			FD_ZERO(&writeSet);
			memset(UserTable_Socket, INVALID_SOCKET, sizeof(SOCKET) * FD_SETSIZE);
			iSessionCount = 0;
		}
	}

	if (iSessionCount >= 0)
	{
		SelectConnect(UserTable_Socket, &writeSet, &errorSet);

		FD_ZERO(&errorSet);
		FD_ZERO(&writeSet);
		memset(UserTable_Socket, INVALID_SOCKET, sizeof(SOCKET) * FD_SETSIZE);
		iSessionCount = 0;
	}
}

void joshua::CNetworkLibraryClient::SelectConnect(SOCKET* pTableSocket, FD_SET* pWriteSet, FD_SET* pErrorSet)
{
	int iResult = 0;
	int iCnt = 0;
	BOOL bProcFlag;

	iResult = select(0, 0, pWriteSet, pErrorSet, &_SelectTimeval);
	if (iResult > 0)
	{
		for (int i = 0; i < FD_SETSIZE; i++)
		{
			if (pTableSocket[i] == INVALID_SOCKET)
				continue;

			if (FD_ISSET(pTableSocket[i], pWriteSet))
			{
				// ���� ����, ���ǰ� Ŭ���̾�Ʈ ����
				RegistSession(pTableSocket[i]);
				continue;
			}

			if (FD_ISSET(pTableSocket[i], pErrorSet))
			{
				// ���� ����
				DWORD error = WSAGetLastError();
				wprintf(L"Connect() error Code : %d\n", error);
			}
		}
	}
	else if (iResult == SOCKET_ERROR)
	{
		wprintf(L"Select() Error!\n");
	}
}

//void joshua::CNetworkLibraryClient::SelectSocket(SOCKET* pTableSocket, FD_SET* pReadSet, FD_SET* pWriteSet)
//{
//	int iResult = 0;
//	int iCnt = 0;
//	BOOL bProcFlag;
//
//	iResult = select(0, pReadSet, pWriteSet, 0, NULL);
//
//	if (iResult > 0)
//	{
//		for (int i = 0; i < FD_SETSIZE; i++)
//		{
//			bProcFlag = TRUE;
//			if (pTableSocket[i] == INVALID_SOCKET)
//				continue;
//
//			// FD_WRITE üũ
//			if (FD_ISSET(pTableSocket[i], pWriteSet))
//			{
//				--iResult;
//				bProcFlag = PostRecv(pTableSocket[i]);
//			}
//
//			if (FD_ISSET(pTableSocket[i], pReadSet))
//			{
//
//				NetWork_Recv(pTableSocket[i]);
//
//			}
//		}
//	}
//	else if (iResult == SOCKET_ERROR)
//	{
//		//PrintError(L"Select() Error!");
//	}
//}

//void joshua::CNetworkLibraryClient::ConnectThread(void)
//{
//	st_SESSION pSession;
//	FD_SET readSet, writeSet;
//	SOCKET UserTable_Socket[FD_SETSIZE];
//	int addrlen, retval;
//	int iSessionCount = 0;
//
//	FD_ZERO(&readSet);
//	FD_ZERO(&writeSet);
//	memset(UserTable_Socket, INVALID_SOCKET, sizeof(SOCKET) * FD_SETSIZE);
//
//	for (int i =0; i != _dwSessionMax; i++)
//	{
//		pSession = _SessionArray[i];
//		if (pSession.socket == INVALID_SOCKET)
//		{
//			// TODO
//			//DisconnectSession(pSession->socket);
//			continue;
//		}
//		// Read Set�� Write Set�� ���
//		UserTable_Socket[iSessionCount] = pSession.socket;
//		FD_SET(pSession.socket, &readSet);
//		if (pSession.SendBuffer.GetUsingCount() > 0)
//		{
//			FD_SET(pSession.socket, &writeSet);
//		}
//
//		iSessionCount++;
//
//		if (iSessionCount >= FD_SETSIZE)
//		{
//			SelectSocket(UserTable_Socket, &readSet, &writeSet);
//
//			FD_ZERO(&readSet);
//			FD_ZERO(&writeSet);
//			memset(UserTable_Socket, INVALID_SOCKET, sizeof(SOCKET) * FD_SETSIZE);
//			iSessionCount = 0;
//		}
//	}
//
//	if (iSessionCount > 0)
//	{
//		SelectSocket(UserTable_Socket, &readSet, &writeSet);
//		FD_ZERO(&readSet);
//		FD_ZERO(&writeSet);
//		memset(UserTable_Socket, INVALID_SOCKET, sizeof(SOCKET) * FD_SETSIZE);
//		iSessionCount = 0;
//	}
//
//}

void joshua::CNetworkLibraryClient::WorkerThread(void)
{
	int retval;
	// �񵿱� ����� �Ϸ� ��ٸ���
	DWORD cbTransferred;
	LPOVERLAPPED pOverlapped;
	st_SESSION* pSession;
	while (true)
	{
		cbTransferred = 0;
		pOverlapped = 0;
		pSession = 0;


		// GQCS return ����� ��.
		// 1. IOCP Queue�κ��� �Ϸ���Ŷ�� ���� �� ������ ��� => TRUE ����, lpCompletionKey ����

		// 2. IOCP Queue�κ��� �Ϸ���Ŷ�� ���� �� ������ ���� ���ÿ� lpOverlapped�� NULL�� ��� => FALSE ����

		// 3. IOCP Queue�κ��� �Ϸ���Ŷ�� ���� �� ������ ���� ���ÿ� 
		//	  lpOverlapped�� NULL�� �ƴ� ����̸鼭 
		//    dequeue�� ��Ұ� ������ I/O�� ���
		// => FALSE����, lpCompletionKey ����

		// 4. IOCP�� ��ϵ� ������ close�� ���
		// => FALSE ����, lpOverlapped�� NULL�� �ƴϰ�,
		// => lpNumberOfBytes�� 0 ����.

		// 5. �������� (PQCS)
		// => lpCompletionKey�� NULL, lpOverlapped�� NULL, lpNumberOfBytes���� NULL

		retval = GetQueuedCompletionStatus(_hCP, &cbTransferred, reinterpret_cast<PULONG_PTR>(&pSession), &pOverlapped, INFINITE);

		if (retval == true)
		{
			if (cbTransferred == 0 && pSession == 0 && pOverlapped == 0)
			{
				PostQueuedCompletionStatus(_hCP, 0, 0, 0);	// GQCS ���� �� ��ġ�� ���Ҹ��Ѱ� �����Ƿ� ����
				break;
			}

			if (cbTransferred == 0)
			{
				if (InterlockedDecrement64(&pSession->lIO->lIOCount) == 0)
				{
					SessionRelease(pSession);
				}
				continue;
			}

			else
			{
				if (pOverlapped == &pSession->RecvOverlapped)
				{
					RecvComplete(pSession, cbTransferred);
				}
				if (pOverlapped == &pSession->SendOverlapped)
				{
					SendComplete(pSession, cbTransferred);
				}


				if (InterlockedDecrement64(&pSession->lIO->lIOCount) == 0)
				{
					SessionRelease(pSession);
				}
			}
		}
		else
		{
			if (cbTransferred == 0)
			{
				if (InterlockedDecrement64(&pSession->lIO->lIOCount) == 0)
				{
					SessionRelease(pSession);
				}
				continue;
				//DisconnectSession(pSession);
			}
			else
			{
				LOG(L"SYSTEM", LOG_ERROR, L"GetQueuedCompletionStatus() failed %d", GetLastError());
				PostQueuedCompletionStatus(_hCP, 0, 0, 0);	// GQCS ���� �� ��ġ�� ���Ҹ��Ѱ� �����Ƿ� ����
				break;
			}
		}
	}
	return;
}

bool joshua::CNetworkLibraryClient::PostSend(st_SESSION* pSession)
{
	while (1)
	{
		if (pSession->SendBuffer.GetUsingCount() == 0)
			return false;
		if (InterlockedCompareExchange(&pSession->bIsSend, TRUE, FALSE) == TRUE)
			return false;
		if (pSession->SendBuffer.GetUsingCount() == 0)
		{
			InterlockedExchange(&pSession->bIsSend, FALSE);
			continue;
		}
		break;
	}

	int iBufCnt;
	WSABUF wsabuf[200];
	CMessage* pPacket;
	int iPacketCnt = pSession->SendBuffer.GetUsingCount();
	for (iBufCnt = 0; iBufCnt < iPacketCnt && iBufCnt < 100; ++iBufCnt)
	{
		if (!pSession->SendBuffer.Peek(pPacket, iBufCnt))
			break;
		wsabuf[iBufCnt].buf = pPacket->GetBufferPtr();
		wsabuf[iBufCnt].len = pPacket->GetDataSize();
	}
	pSession->dwPacketCount = iBufCnt;
	DWORD dwTransferred = 0;
	ZeroMemory(&pSession->SendOverlapped, sizeof(OVERLAPPED));
	InterlockedIncrement64(&pSession->lIO->lIOCount);
	if (WSASend(pSession->socket, wsabuf, iBufCnt, &dwTransferred, 0, &pSession->SendOverlapped, NULL) == SOCKET_ERROR)
	{
		int error = WSAGetLastError();

		// WSASend�� pending�� �ɸ� ���� ���� send �ӽ� ���۰� ������ ����
		if (error != WSA_IO_PENDING)
		{
			if (error != 10038 && error != 10053 && error != 10054 && error != 10058)
				LOG(L"SYSTEM", LOG_ERROR, L"WSASend() # failed%d / Socket:%d / IOCnt:%d / SendQ Size:%d", error, pSession->socket, pSession->lIO->lIOCount, pSession->SendBuffer.GetUsingCount());

			//DisconnectSession(pSession);
			if (InterlockedDecrement64(&pSession->lIO->lIOCount) == 0)
				SessionRelease(pSession);

			return false;
		}
	}

	return true;
}

bool joshua::CNetworkLibraryClient::PostRecv(st_SESSION* pSession)
{
	WSABUF wsabuf[2];
	int iBufCnt = 1;
	wsabuf[0].buf = pSession->RecvBuffer.GetWriteBufferPtr();
	wsabuf[0].len = pSession->RecvBuffer.GetNotBrokenPutSize();

	if (pSession->RecvBuffer.GetNotBrokenPutSize() < pSession->RecvBuffer.GetFreeSize())
	{
		wsabuf[1].buf = pSession->RecvBuffer.GetBufferPtr();
		wsabuf[1].len = pSession->RecvBuffer.GetFreeSize() - wsabuf[0].len;
		++iBufCnt;
	}

	DWORD dwTransferred = 0;
	DWORD dwFlag = 0;
	ZeroMemory(&pSession->RecvOverlapped, sizeof(OVERLAPPED));
	InterlockedIncrement64(&pSession->lIO->lIOCount);
	//wprintf(L"IO COUNT : %d\n", pSession->dwIOCount);
	if (WSARecv(pSession->socket, wsabuf, iBufCnt, &dwTransferred, &dwFlag, &pSession->RecvOverlapped, NULL) == SOCKET_ERROR)
	{
		int error = WSAGetLastError();

		if (error != WSA_IO_PENDING)
		{			// WSAENOTSOCK(10038) : ������ �ƴ� �׸� ���� �۾� �õ�
			// WSAECONNABORTED(10053) : ȣ��Ʈ�� ���� ����. ������ ���� �ð� �ʰ�, �������� ���� �߻�
			// WSAECONNRESET(10054) : ���� ȣ��Ʈ�� ���� ���� ���� ���� ����. ���� ȣ��Ʈ�� ���ڱ� �����ǰų� �ٽ� ���۵ǰų� �ϵ� ���Ḧ ����ϴ� ���
			// WSAESHUTDOWN(10058) : ���� ���� �� ����
			if (error != 10038 && error != 10053 && error != 10054 && error != 10058)
				LOG(L"SYSTEM", LOG_ERROR, L"WSASend() # failed%d / Socket:%d / IOCnt:%d / SendQ Size:%d", error, pSession->socket, pSession->lIO->lIOCount, pSession->SendBuffer.GetUsingCount());

			//DisconnectSession(pSession);
			if (InterlockedDecrement64(&pSession->lIO->lIOCount) == 0)
				SessionRelease(pSession);

			return false;

		}
	}

	return true;
}

void joshua::CNetworkLibraryClient::RegistSession(SOCKET sock)
{
	if (sock == INVALID_SOCKET)
	{
		WCHAR szParam[16] = { 0 };
		InetNtop(AF_INET, &_serveraddr.sin_addr, szParam, 16);
		LOG(L"SERVER", LOG_ERROR, L"%s client IP : %s, port : %d\n", L"accept() Error!", szParam, ntohs(_serveraddr.sin_port));
		DisconnectSocket(sock);
		return;
	}

	if (OnConnectionRequest(&_serveraddr) == FALSE)
	{
		WCHAR szParam[16] = { 0 };
		InetNtop(AF_INET, &_serveraddr.sin_addr, szParam, 16);
		LOG(L"SERVER", LOG_WARNNING, L"client IP : %s, port : %d connection denied\n", szParam, ntohs(_serveraddr.sin_port));
		DisconnectSocket(sock);
		return;
	}

	if (_dwSessionCount > _dwSessionMax)
	{
		WCHAR szParam[16] = { 0 };
		InetNtop(AF_INET, &_serveraddr.sin_addr, szParam, 16);
		LOG(L"SERVER", LOG_WARNNING, L"client IP : %s, port : %d connection denied, MaxClient Over\n", szParam, ntohs(_serveraddr.sin_port));
		DisconnectSocket(sock);
		return;
	}
	st_SESSION* pSession = InsertSession(sock, &_serveraddr);
	if (pSession == nullptr)
	{
		// ���� ������
		// TODO dump���� ����
		InterlockedDecrement64(&_dwSessionCount);
		DisconnectSocket(sock);
		return;
	}
	// ���ϰ� ����� �Ϸ� ��Ʈ ����
	if (CreateIoCompletionPort((HANDLE)sock, (HANDLE)_hCP, (ULONG_PTR)pSession, 0) == NULL)
	{
		//  HANDLE ���ڿ� ������ �ƴ� ���� �� ��� �߸��� �ڵ�(6�� ����) �߻�
		// ������ �ƴ� ���� �־��ٴ� ���� �ٸ� �����忡�� ������ ��ȯ�ߴٴ� �ǹ��̹Ƿ� ����ȭ ������ ���ɼ��� ����.
		LOG(L"SYSTEM", LOG_ERROR, L"AcceptThread() - CreateIoCompletionPort() failed : %d", WSAGetLastError());
		if (InterlockedDecrement64(&pSession->lIO->lIOCount) == 0)
		{
			//DisconnectSession(pSession);
			SessionRelease(pSession);
			return;
		}
	}
	pSession->lIO->bIsReleased = FALSE;

	OnClientJoin(&pSession->clientaddr, pSession->SessionID);

	PostRecv(pSession);

	if (InterlockedDecrement64(&pSession->lIO->lIOCount) == 0)
	{
		SessionRelease(pSession);
	}
}


// Start
BOOL joshua::CNetworkLibraryClient::Start(DWORD port, BOOL nagle, const WCHAR* ip, DWORD threadCount, __int64 MaxClient)
{
	// ���� �ʱ�ȭ
	_dwSessionMax = MaxClient;
	_bServerOn = TRUE;

	if (InitialNetwork(ip, port, nagle) == false)
	{
		LOG(L"SERVER", LOG_ERROR, L"%s\n", L"Initialize failed!");
		return FALSE;
	}

	// IOCP ����
	_hCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
	if (_hCP == NULL)
	{
		LOG(L"SERVER", LOG_ERROR, L"%s\n", L"CreateIoCompletionPort() ERROR");
		return FALSE;
	}

	// ������ ����
	if (CreateThread(threadCount) == FALSE)
	{
		LOG(L"SERVER", LOG_ERROR, L"%s\n", L"CreateThread() ERROR");
		return FALSE;
	}

	// ���� �Ҵ� �� �ε��� ���� ����
	if (CreateSession() == FALSE)
	{
		LOG(L"SERVER", LOG_ERROR, L"%s\n", L"CreateSession() ERROR");
		return FALSE;
	}
	CreateSocket();
	ConnectToServer();
	return TRUE;
}

void joshua::CNetworkLibraryClient::SessionRelease(st_SESSION* pSession)
{
	st_SESSION_FLAG temp(0, FALSE);
	if (!InterlockedCompareExchange128((LONG64*)pSession->lIO, TRUE, 0, (LONG64*)&temp))
		return;

	DisconnectSession(pSession);

	OnClientLeave(pSession->SessionID);

	ZeroMemory(&pSession->clientaddr, sizeof(SOCKADDR_IN));
	CMessage* pPacket = nullptr;
	while (pSession->SendBuffer.Dequeue(pPacket))
	{
		if (pPacket != nullptr)
			pPacket->SubRef();
		pPacket = nullptr;
	}

	InterlockedExchange(&pSession->bIsSend, FALSE);
	pSession->SessionID = -1;
	pSession->socket = INVALID_SOCKET;
	pSession->dwPacketCount = 0;
	pSession->lMessageList.clear();
	PushIndex(pSession->index);
	InterlockedDecrement64(&_dwSessionCount);

}

void joshua::CNetworkLibraryClient::DisconnectSocket(SOCKET sock)
{
	LINGER optval;
	int retval;
	optval.l_onoff = 1;
	optval.l_linger = 0;

	retval = setsockopt(sock, SOL_SOCKET, SO_LINGER, (char*)&optval, sizeof(optval));
	closesocket(sock);
}

void joshua::CNetworkLibraryClient::SendPacket(UINT64 id, CMessage* message)
{
	st_SESSION* pSession = SessionReleaseCheck(id);
	if (pSession == nullptr)
		return;

	message->AddRef();
	pSession->SendBuffer.Enqueue(message);

	PostSend(pSession);

	InterlockedIncrement64(&_lSendTPS);

	if (InterlockedDecrement64(&pSession->lIO->lIOCount) == 0)
		SessionRelease(pSession);

	return;
}

void joshua::CNetworkLibraryClient::Stop()
{
	SYSTEM_INFO si;
	GetSystemInfo(&si);
	_bServerOn = FALSE;

	closesocket(_slisten_socket);

	WaitForSingleObject(_AcceptThread, INFINITE);

	for (int i = 0; i < si.dwNumberOfProcessors * 2; i++)
	{
		PostQueuedCompletionStatus(_hCP, 0, 0, 0);
	}

	WaitForMultipleObjects(_iThreadCount, (HANDLE*)&_ThreadVector, TRUE, INFINITE);

	WSACleanup();

	CloseHandle(_hCP);

	for (int i = 0; i < MAX_CLIENT_COUNT; i++)
	{
		if (_SessionArray[i].socket != INVALID_SOCKET)
			SessionRelease(&_SessionArray[i]);
	}

	if (_SessionArray != NULL)
	{
		delete[] _SessionArray;
		_SessionArray = NULL;
	}

	_slisten_socket = INVALID_SOCKET;
	_dwSessionID = 0;
	_dwSessionCount = _dwSessionMax = 0;
	_hCP = INVALID_HANDLE_VALUE;
	ZeroMemory(&_serveraddr, sizeof(_serveraddr));
	_bNagle = FALSE;
	_dwCount = 0;
	_iThreadCount = 0;
	_ThreadVector.clear();
	for (int i = 0; i < _ArrayIndex.size(); i++)
	{
		_ArrayIndex.pop();
	}
}

void joshua::CNetworkLibraryClient::PrintPacketCount()
{
	wprintf(L"==================== IOCP Echo Server Test ====================\n");
	wprintf(L" - SessionCount : %lld\n", _dwSessionCount);
	wprintf(L" - Accept Count : %08lld\n", _lAcceptCount);
	wprintf(L" - Accept TPS : %08lld\n", _lAcceptTPS);
	wprintf(L" - Send TPS : %08lld\n", _lSendTPS);
	wprintf(L" - Recv TPS : %08lld\n", _lRecvTPS);
	//wprintf(L" - PacketAlloc : %08d\n", CMessage::GetPacketAllocSize());
	wprintf(L" - PacketUse : %08d\n", CMessage::GetPacketUsingSize());
	wprintf(L"===============================================================\n");
	_lAcceptTPS = _lSendTPS = _lRecvTPS = 0;

}

void joshua::CNetworkLibraryClient::DisconnectSession(st_SESSION* pSession)
{
	LINGER optval;
	int retval;
	optval.l_onoff = 1;
	optval.l_linger = 0;

	retval = setsockopt(pSession->socket, SOL_SOCKET, SO_LINGER, (char*)&optval, sizeof(optval));
	closesocket(pSession->socket);

}

joshua::st_SESSION* joshua::CNetworkLibraryClient::SessionReleaseCheck(UINT64 iSessionID)
{
	// multi-thread ȯ�濡�� release, connectm send, accept ���� ���ÿ� �߻��� �� ������ �����ؾ� �Ѵ�.
	int nIndex = GetSessionIndex(iSessionID);
	st_SESSION* pSession = &_SessionArray[nIndex];
	//st_SESSION* pSession = nullptr;
	//for (int i = 0; i < MAX_CLIENT_COUNT; i++)
	//{
	//	if (_SessionArray[i].SessionID == iSessionID)
	//	{
	//		pSession = &_SessionArray[i];
	//		break;
	//	}
	//}

	// �̹� ���� �Ǿ��ٸ�(�̹� ���� �ڵ带 ���ٸ�)
	if (pSession->lIO->bIsReleased == TRUE)
		return nullptr;

	// ioCount�� �����ؼ� 1�̶�� ���� ��𼱰� �� ���ǿ� ���� release�� ����ǰ� �ִٴ� ��.
	if (InterlockedIncrement64(&pSession->lIO->lIOCount) == 1) 
	{
		if (InterlockedDecrement64(&pSession->lIO->lIOCount) == 0)
			SessionRelease(pSession);

		return nullptr;
	}

	// ������ �˻��ؼ� ������� �̹� �ٸ� �������� ��ü�� ���
	if (pSession->SessionID != iSessionID)
	{
		if (InterlockedDecrement64(&pSession->lIO->lIOCount) == 0)
		{
			SessionRelease(pSession);
		}
		return nullptr;
	}

	if (pSession->lIO->bIsReleased == FALSE)
		return pSession;

	if (InterlockedDecrement64(&pSession->lIO->lIOCount) == 0)
		SessionRelease(pSession);

	//if (pSession == nullptr)
	//	return nullptr;


	return nullptr;
}

void joshua::CNetworkLibraryClient::RecvComplete(st_SESSION* pSession, DWORD dwTransferred)
{	// ���ŷ���ŭ RecvQ.MoveWritePos �̵�
	pSession->RecvBuffer.MoveWritePos(dwTransferred);

	// Header�� Payload ���̸� ���� 2Byte ũ���� Ÿ��
	WORD wHeader;
	while (1)
	{
		// 1. RecvQ�� 'sizeof(Header)'��ŭ �ִ��� Ȯ��
		int iRecvSize = pSession->RecvBuffer.GetUseSize();
		if (iRecvSize < sizeof(wHeader))
		{
			// �� �̻� ó���� ��Ŷ�� ����
			// ����, Header ũ�⺸�� ���� ������ �����Ͱ� �����ȴٸ� 2������ �ɷ���
			break;
		}

		// 2. Packet ���� Ȯ�� : Headerũ��('sizeof(Header)') + Payload����('Header')
		pSession->RecvBuffer.Peek(reinterpret_cast<char*>(&wHeader), sizeof(wHeader));

		if(wHeader != 8)
		{			// Header�� Payload�� ���̰� ������ �ٸ�
			DisconnectSession(pSession);
			LOG(L"SYSTEM", LOG_WARNNING, L"Header Error");
			break;
		}

		if (iRecvSize < sizeof(wHeader) + wHeader)
		{
			// Header�� Payload�� ���̰� ������ �ٸ�
			DisconnectSession(pSession);
			LOG(L"SYSTEM", LOG_WARNNING, L"Header & PayloadLength mismatch");
			break;
		}

		CMessage* pPacket = CMessage::Alloc();

		(*pPacket) << wHeader;

		//// 3. Payload ���� Ȯ�� : PacketBuffer�� �ִ� ũ�⺸�� Payload�� Ŭ ���
		//if (pPacket->GetBufferSize() < wHeader)
		//{
		//	pPacket->SubRef();
		//	DisconnectSession(pSession);
		//	LOG(L"SYSTEM", LOG_WARNNING, L"PacketBufferSize < PayloadSize ");
		//	break;
		//}
		pSession->RecvBuffer.RemoveData(sizeof(wHeader));

		// 4. PacketPool�� Packet ������ �Ҵ�
		if (pSession->RecvBuffer.Peek(pPacket->GetBufferPtr() + 2, wHeader) != wHeader)
		{
			pPacket->SubRef();
			LOG(L"SYSTEM", LOG_WARNNING, L"RecvQ dequeue error");
			DisconnectSession(pSession);
			break;
		}

		pPacket->MoveWritePos(wHeader);
		//UINT64 data;
		//memcpy(&data, pPacket->GetBufferPtr() + 2, 8);
		////wprintf(L"Recv data : %08ld\n", data);
		// 5. Packet ó��
		pSession->RecvBuffer.RemoveData(wHeader);
		InterlockedIncrement64(&_lRecvTPS);

		OnRecv(pSession->SessionID, pPacket);

		pPacket->SubRef();
	}
	PostRecv(pSession);
}

void joshua::CNetworkLibraryClient::SendComplete(st_SESSION* pSession, DWORD dwTransferred)
{
	OnSend(pSession->SessionID, dwTransferred);

	// ���� ��Ŷ �� ��ŭ �����
	CMessage* pPacket;
	for (int i = 0; i < pSession->dwPacketCount; ++i)
	{
		if (pSession->SendBuffer.Dequeue(pPacket))
		{
			pPacket->SubRef();
		}
	}
	pSession->dwPacketCount = 0;

	InterlockedExchange(&pSession->bIsSend, FALSE);
	PostSend(pSession);
}
