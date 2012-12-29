/**
 Copyright (C) 2012 Nils Weiss, Patrick Bruenn.
 
 This file is part of Wifly_Light.
 
 Wifly_Light is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
 
 Wifly_Light is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with Wifly_Light.  If not, see <http://www.gnu.org/licenses/>. */

#include "BroadcastReceiver.h"

#include <cstring>
#include <functional>
#include <iostream>
#include <stdio.h>
#include <sstream>

const char BroadcastReceiver::BROADCAST_DEVICE_ID[] = "WiFly";
const size_t BroadcastReceiver::BROADCAST_DEVICE_ID_LENGTH = 5;
const char BroadcastReceiver::STOP_MSG[] = "StopThread";
const size_t BroadcastReceiver::STOP_MSG_LENGTH = sizeof(STOP_MSG);

BroadcastReceiver::BroadcastReceiver(unsigned short port) : mPort(port), mThread(boost::ref(*this))
{
}

BroadcastReceiver::~BroadcastReceiver(void)
{
	Stop();
}

void BroadcastReceiver::operator()(void)
{
	UdpSocket udpSock(0x7f000001, mPort, true);
	sockaddr_storage remoteAddr;
	size_t remoteAddrLength = sizeof(remoteAddr);
	
	for(;;)
	{
		BroadcastMessage msg;
		size_t bytesRead = udpSock.RecvFrom((unsigned char*)&msg, sizeof(msg), NULL, (sockaddr*)&remoteAddr, &remoteAddrLength);
		// received a Wifly broadcast?
		if((sizeof(msg) == bytesRead) && (0 == memcmp(msg.deviceId, BROADCAST_DEVICE_ID, BROADCAST_DEVICE_ID_LENGTH)))
		{
#ifdef DEBUG
			msg.NetworkToHost();
			msg.Print(std::cout);
			std::cout << std::hex << ntohl(((sockaddr_in*)&remoteAddr)->sin_addr.s_addr) << '\n';
#endif
			mMutex.lock();
			mIpTable.push_back(new Endpoint(remoteAddr, remoteAddrLength));
			mMutex.unlock();
		}
		// received a stop event?
		else if(/*TODO remote.address().is_loopback()
					&&*/ (STOP_MSG_LENGTH == bytesRead) 
					&& (0 == memcmp(&msg, STOP_MSG, bytesRead)))
		{
			return;
		}
	}
}

uint32_t BroadcastReceiver::GetIp(size_t index) const
{
	return mIpTable[index]->m_Addr;
}

uint16_t BroadcastReceiver::GetPort(size_t index) const
{
	return mIpTable[index]->m_Port;
}

size_t BroadcastReceiver::NumRemotes(void) const
{
	return mIpTable.size();
}

void BroadcastReceiver::ShowRemotes(std::ostream& out) const
{
	size_t index = 0;
	for(vector<Endpoint*>::const_iterator it = mIpTable.begin(); it != mIpTable.end(); *it++, index++)
	{
		out << index << ':' << std::hex << (*it)->m_Addr << '\n';
	}
}

void BroadcastReceiver::Stop(void)
{
	UdpSocket sock(0x7F000001, mPort, false);
	sock.Send((unsigned char const*)STOP_MSG, STOP_MSG_LENGTH);
	mThread.join();
	return;
}

