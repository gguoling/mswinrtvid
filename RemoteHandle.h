/*
RemoteHandle.h

mediastreamer2 library - modular sound and video processing and streaming
Windows Audio Session API sound card plugin for mediastreamer2
Copyright (C) 2010-2015 Belledonne Communications, Grenoble, France

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#pragma once

#include <ppltasks.h>

namespace libmswinrtvid
{
	class RemoteHandle
	{
	public:
		RemoteHandle();
		~RemoteHandle();
		RemoteHandle& AssignHandle(HANDLE localHandle, DWORD processId);
		RemoteHandle& Close();
		HANDLE GetLocalHandle() const;
		HANDLE GetRemoteHandle() const;
	private:
		RemoteHandle(const RemoteHandle&);
		const RemoteHandle& operator = (const RemoteHandle&) { return *this; };
		HANDLE mLocalHandle;
		HANDLE mRemoteHandle;
		DWORD mProcessId;
		HANDLE mProcessHandle;
	};
}
