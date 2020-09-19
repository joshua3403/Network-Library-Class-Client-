#include "CNetworkTest.h"

void Network::OnClientJoin(SOCKADDR_IN* sockAddr, UINT64 sessionID)
{
}

void Network::OnClientLeave(UINT64 sessionID)
{
}

bool Network::OnConnectionRequest(SOCKADDR_IN* sockAddr)
{
    return true;
}

void Network::OnRecv(UINT64 sessionID, CMessage* message)
{
    SendPacket(sessionID, message);
}

void Network::OnSend(UINT64 sessionID, int sendsize)
{
}

void Network::OnError(int errorcode, WCHAR*)
{
}
