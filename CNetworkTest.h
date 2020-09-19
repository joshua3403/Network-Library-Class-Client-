#pragma once
#include "stdafx.h"
#include "CNetWorkLibrary(MemoryPool).h"
class Network : public joshua::CNetworkLibraryClient
{
public:

public:
	void OnClientJoin(SOCKADDR_IN* sockAddr, UINT64 sessionID);
	virtual void OnClientLeave(UINT64 sessionID);

	// accept직후
	// TRUE면 접속 허용
	// FALSE면 접속 불허
	bool OnConnectionRequest(SOCKADDR_IN* sockAddr);

	void OnRecv(UINT64 sessionID, CMessage* message);
	void OnSend(UINT64 sessionID, int sendsize);

	//	virtual void OnWorkerThreadBegin() = 0;                    
	//	virtual void OnWorkerThreadEnd() = 0;                      

	void OnError(int errorcode, WCHAR*);
};