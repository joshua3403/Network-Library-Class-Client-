#pragma once
#include "stdafx.h"
#include "CNewRingBuffer.h"
#include "Stack(LockFree).h"
#include "Queue(LockFree).h"
#include "CMessage.h"
#include "CLog.h"

#define READ 3
#define WRITE 5
#define MAX_CLIENT_COUNT 1000

// sizeof(UINT64) == 8 Byte == 64 Bit
// [00000000 00000000 0000] [0000 00000000 00000000 00000000 00000000 00000000]
// 1. 상위 2.5 Byte = Index 영역
// 2. 하위 5.5 Byte = SessionID 영역
#define CreateSessionID(ID, Index)	(((UINT64)Index << 44) | ID)				
#define GetSessionIndex(SessionID)	((SessionID >> 44) & 0xfffff)
#define GetSessionID(SessionID)		(SessionID & 0x00000fffffffffff)
namespace joshua
{
	struct st_SESSION_FLAG
	{

		st_SESSION_FLAG(UINT64 ioCount, UINT64 isReleased)
		{
			lIOCount = ioCount;
			bIsReleased = isReleased;
		}
		LONG64		lIOCount;
		LONG64		bIsReleased;
	};



	struct st_SESSION
	{
		int index;
		UINT64 SessionID;
		SOCKET socket;
		SOCKADDR_IN clientaddr;
		CQueue<CMessage*> SendBuffer;
		RingBuffer RecvBuffer;
		OVERLAPPED SendOverlapped;
		OVERLAPPED RecvOverlapped;
		LONG bIsSend;
		st_SESSION_FLAG* lIO;

		DWORD dwPacketCount;
		std::list<CMessage*> lMessageList;


		// SessionID가 0이면 사용하지 않는 세션
		st_SESSION()
		{
			dwPacketCount = 0;
			SessionID = -1;
			bIsSend = FALSE;
			socket = INVALID_SOCKET;
			ZeroMemory(&clientaddr, sizeof(clientaddr));
			index = 0;
			SendBuffer.Clear();
			RecvBuffer.ClearBuffer();
			lIO = (st_SESSION_FLAG*)_aligned_malloc(sizeof(st_SESSION_FLAG), 16);
			lIO->bIsReleased = FALSE;
			lIO->lIOCount = 0;
		}

		~st_SESSION()
		{
			_aligned_free(lIO);
		}

	};


	class CNetworkLibraryClient
	{
	private:
		SOCKET _slisten_socket;
		BOOL _bServerOn;
		BOOL _bNagle;

		// 입출력 완료 포트 생성// IOCP 변수
		HANDLE _hCP;

		LONG64 _dwSessionCount;
		LONG64 _dwSessionMax;
		LONG64 _dwCount;
		LONG64 _dwMaxClientCount;

		// Thread
		HANDLE _AcceptThread;
		int _iThreadCount;
		std::vector<HANDLE> _ThreadVector;

		// Session
		SOCKADDR_IN _serveraddr;
		UINT64 _dwSessionID;
		st_SESSION* _SessionArray;
		std::stack<UINT64> _ArrayIndex;

		CRITICAL_SECTION _IndexStackCS;

		LONG64 _lSendTPS;
		LONG64 _lRecvTPS;
		LONG64 _lAcceptTPS;
		LONG64 _lAcceptCount;

		std::vector<SOCKET> _SocketVector;

		TIMEVAL _SelectTimeval;

	private:
		// 소켓 초기화
		BOOL InitialNetwork(const WCHAR* ip, DWORD port, BOOL Nagle);
		// 스레드 생성
		// 0이면 프로세서 * 2
		BOOL CreateThread(DWORD threadCount);
		// 세션 할당 및 생성
		BOOL CreateSession();

		void PushIndex(UINT64 index);

		UINT64 PopIndex();

		// 빈 세션 공간에 새로 생성된 세션 세팅
		st_SESSION* InsertSession(SOCKET sock, SOCKADDR_IN* sockaddr);

		// Accept를 전담할 스레드의 시작 함수
		//static unsigned int WINAPI ConnectThread(LPVOID lpParam);
		static unsigned int WINAPI WorkerThread(LPVOID lpParam);

		void CreateSocket();

		void ConnectToServer();

		void SelectConnect(SOCKET* pTableSocket, FD_SET* pWriteSet, FD_SET* pErrorSet);
		//void SelectSocket(SOCKET* pTableSocket, FD_SET* pReadSet, FD_SET* pWriteSet);

		//void ConnectThread(void);
		void WorkerThread(void);

		bool PostSend(st_SESSION* session);
		bool PostRecv(st_SESSION* session);

		void RegistSession(SOCKET sock);

		void SessionRelease(st_SESSION* session);
		void DisconnectSocket(SOCKET sock);
		void DisconnectSession(st_SESSION* pSession);
		joshua::st_SESSION* SessionReleaseCheck(UINT64 iSessionID);

		// IOCP completion notice
		void RecvComplete(st_SESSION* pSession, DWORD dwTransferred);
		void SendComplete(st_SESSION* pSession, DWORD dwTransferred);

	protected:
		// Accept후 접속 처리 완료후 호출하는 함수
		virtual void OnClientJoin(SOCKADDR_IN* sockAddr, UINT64 sessionID) = 0;
		virtual void OnClientLeave(UINT64 sessionID) = 0;

		// accept직후
		// TRUE면 접속 허용
		// FALSE면 접속 불허
		virtual bool OnConnectionRequest(SOCKADDR_IN* sockAddr) = 0;

		virtual void OnRecv(UINT64 sessionID, CMessage* message) = 0;
		virtual void OnSend(UINT64 sessionID, int sendsize) = 0;

		//	virtual void OnWorkerThreadBegin() = 0;                    
		//	virtual void OnWorkerThreadEnd() = 0;                      

		virtual void OnError(int errorcode, WCHAR*) = 0;
		 
		void SendPacket(UINT64 id, CMessage* message);
		bool Dissconnect(UINT64 id, CMessage* message);
	public:

		CNetworkLibraryClient()
		{
			_slisten_socket = INVALID_SOCKET;
			_dwSessionID = 0;
			_dwSessionCount = _dwSessionMax = 0;
			_hCP = INVALID_HANDLE_VALUE;
			_SessionArray = nullptr;
			ZeroMemory(&_serveraddr, sizeof(_serveraddr));
			InitializeCriticalSection(&_IndexStackCS);
			_bNagle = FALSE;
			_dwCount = 0;
			_iThreadCount = 0;
			_lSendTPS = 0;
			_lRecvTPS = 0;
			_lAcceptTPS = 0;
			_lAcceptCount = 0;
			_SelectTimeval.tv_sec = 0;
			_SelectTimeval.tv_usec = 300000;
		}
		
		~CNetworkLibraryClient()
		{
			delete[] _SessionArray;
			closesocket(_slisten_socket);
			CloseHandle(_hCP);
			WSACleanup();
		}

		BOOL Start(DWORD port, BOOL nagle, const WCHAR* ip = nullptr, DWORD threadCount = 0, __int64 MaxClient = 0);

		void Stop();

		void ReStart();

		void PrintPacketCount();


	};
}
