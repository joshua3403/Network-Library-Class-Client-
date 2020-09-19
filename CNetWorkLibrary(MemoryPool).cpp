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
	// 스레드 제한
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
		// TCP 소켓에서 좀비세션 처리
		// 좀비 세션은 비정상적으로 종료(랜선 문제, 혹은 외부에서 세션을 일부러 끊은 경우)되어 서버나 클라이언트에서
		// 세션이 종료된지 모르는 상태의 세션을 의미 (Close 이벤트를 받지 못함)
		// TCP KeepAlive사용
				// - SO_KEEPALIVE : 시스템 레지스트리 값 변경. 시스템의 모든 SOCKET에 대해서 KEEPALIVE 설정
		// - SIO_KEEPALIVE_VALS : 특정 SOCKET만 KEEPALIVE 설정
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

		// 넌블록 소켓으로 전환
		u_long on = 1;
		retval = ioctlsocket(sock, FIONBIO, &on);
		if (retval == SOCKET_ERROR)
		{
			wprintf(L"ioctlsocket() eror\n");
			return;
		}

		// 네이글 알고리즘 해제
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
				// 접속 성공, 세션과 클라이언트 생성
				RegistSession(pTableSocket[i]);
				continue;
			}

			if (FD_ISSET(pTableSocket[i], pErrorSet))
			{
				// 접속 실패
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
//			// FD_WRITE 체크
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
//		// Read Set과 Write Set에 등록
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
	// 비동기 입출력 완료 기다리기
	DWORD cbTransferred;
	LPOVERLAPPED pOverlapped;
	st_SESSION* pSession;
	while (true)
	{
		cbTransferred = 0;
		pOverlapped = 0;
		pSession = 0;


		// GQCS return 경우의 수.
		// 1. IOCP Queue로부터 완료패킷을 얻어내는 데 성공한 경우 => TRUE 리턴, lpCompletionKey 세팅

		// 2. IOCP Queue로부터 완료패킷을 얻어내는 데 실패한 경우와 동시에 lpOverlapped가 NULL인 경우 => FALSE 리턴

		// 3. IOCP Queue로부터 완료패킷을 얻어내는 데 성공한 경우와 동시에 
		//	  lpOverlapped가 NULL이 아닌 경우이면서 
		//    dequeue한 요소가 실패한 I/O인 경우
		// => FALSE리턴, lpCompletionKey 세팅

		// 4. IOCP에 등록된 소켓이 close된 경우
		// => FALSE 리턴, lpOverlapped에 NULL이 아니고,
		// => lpNumberOfBytes에 0 세팅.

		// 5. 정상종료 (PQCS)
		// => lpCompletionKey는 NULL, lpOverlapped는 NULL, lpNumberOfBytes또한 NULL

		retval = GetQueuedCompletionStatus(_hCP, &cbTransferred, reinterpret_cast<PULONG_PTR>(&pSession), &pOverlapped, INFINITE);

		if (retval == true)
		{
			if (cbTransferred == 0 && pSession == 0 && pOverlapped == 0)
			{
				PostQueuedCompletionStatus(_hCP, 0, 0, 0);	// GQCS 에러 시 조치를 취할만한게 없으므로 종료
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
				PostQueuedCompletionStatus(_hCP, 0, 0, 0);	// GQCS 에러 시 조치를 취할만한게 없으므로 종료
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

		// WSASend가 pending이 걸릴 경우는 현재 send 임시 버퍼가 가득찬 상태
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
		{			// WSAENOTSOCK(10038) : 소켓이 아닌 항목에 소켓 작업 시도
			// WSAECONNABORTED(10053) : 호스트가 연결 중지. 데이터 전송 시간 초과, 프로토콜 오류 발생
			// WSAECONNRESET(10054) : 원격 호스트에 의해 기존 연결 강제 해제. 원격 호스트가 갑자기 중지되거나 다시 시작되거나 하드 종료를 사용하는 경우
			// WSAESHUTDOWN(10058) : 소켓 종료 후 전송
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
		// 서버 꺼야함
		// TODO dump내고 끄자
		InterlockedDecrement64(&_dwSessionCount);
		DisconnectSocket(sock);
		return;
	}
	// 소켓과 입출력 완료 포트 연결
	if (CreateIoCompletionPort((HANDLE)sock, (HANDLE)_hCP, (ULONG_PTR)pSession, 0) == NULL)
	{
		//  HANDLE 인자에 소켓이 아닌 값이 올 경우 잘못된 핸들(6번 에러) 발생
		// 소켓이 아닌 값을 넣었다는 것은 다른 스레드에서 소켓을 반환했다는 의미이므로 동기화 문제일 가능성이 높다.
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
	// 소켓 초기화
	_dwSessionMax = MaxClient;
	_bServerOn = TRUE;

	if (InitialNetwork(ip, port, nagle) == false)
	{
		LOG(L"SERVER", LOG_ERROR, L"%s\n", L"Initialize failed!");
		return FALSE;
	}

	// IOCP 생성
	_hCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
	if (_hCP == NULL)
	{
		LOG(L"SERVER", LOG_ERROR, L"%s\n", L"CreateIoCompletionPort() ERROR");
		return FALSE;
	}

	// 스레드 생성
	if (CreateThread(threadCount) == FALSE)
	{
		LOG(L"SERVER", LOG_ERROR, L"%s\n", L"CreateThread() ERROR");
		return FALSE;
	}

	// 세션 할당 및 인덱스 스택 설정
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
	// multi-thread 환경에서 release, connectm send, accept 등이 동시에 발생할 수 있음을 생각해야 한다.
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

	// 이미 해제 되었다면(이미 해제 코드를 탔다면)
	if (pSession->lIO->bIsReleased == TRUE)
		return nullptr;

	// ioCount가 증가해서 1이라는 뜻은 어디선가 이 세션에 대한 release가 진행되고 있다는 뜻.
	if (InterlockedIncrement64(&pSession->lIO->lIOCount) == 1) 
	{
		if (InterlockedDecrement64(&pSession->lIO->lIOCount) == 0)
			SessionRelease(pSession);

		return nullptr;
	}

	// 세션을 검색해서 얻었으나 이미 다른 세션으로 대체된 경우
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
{	// 수신량만큼 RecvQ.MoveWritePos 이동
	pSession->RecvBuffer.MoveWritePos(dwTransferred);

	// Header는 Payload 길이를 담은 2Byte 크기의 타입
	WORD wHeader;
	while (1)
	{
		// 1. RecvQ에 'sizeof(Header)'만큼 있는지 확인
		int iRecvSize = pSession->RecvBuffer.GetUseSize();
		if (iRecvSize < sizeof(wHeader))
		{
			// 더 이상 처리할 패킷이 없음
			// 만약, Header 크기보다 작은 모종의 데이터가 누적된다면 2번에서 걸러짐
			break;
		}

		// 2. Packet 길이 확인 : Header크기('sizeof(Header)') + Payload길이('Header')
		pSession->RecvBuffer.Peek(reinterpret_cast<char*>(&wHeader), sizeof(wHeader));

		if(wHeader != 8)
		{			// Header의 Payload의 길이가 실제와 다름
			DisconnectSession(pSession);
			LOG(L"SYSTEM", LOG_WARNNING, L"Header Error");
			break;
		}

		if (iRecvSize < sizeof(wHeader) + wHeader)
		{
			// Header의 Payload의 길이가 실제와 다름
			DisconnectSession(pSession);
			LOG(L"SYSTEM", LOG_WARNNING, L"Header & PayloadLength mismatch");
			break;
		}

		CMessage* pPacket = CMessage::Alloc();

		(*pPacket) << wHeader;

		//// 3. Payload 길이 확인 : PacketBuffer의 최대 크기보다 Payload가 클 경우
		//if (pPacket->GetBufferSize() < wHeader)
		//{
		//	pPacket->SubRef();
		//	DisconnectSession(pSession);
		//	LOG(L"SYSTEM", LOG_WARNNING, L"PacketBufferSize < PayloadSize ");
		//	break;
		//}
		pSession->RecvBuffer.RemoveData(sizeof(wHeader));

		// 4. PacketPool에 Packet 포인터 할당
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
		// 5. Packet 처리
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

	// 보낸 패킷 수 만큼 지우기
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
