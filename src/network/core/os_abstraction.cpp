/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file os_abstraction.cpp OS specific implementations of functions of the OS abstraction layer for network stuff.
 *
 * The general idea is to have simple abstracting functions for things that
 * require different implementations for different environments.
 * In here the functions, and their documentation, are defined only once
 * and the implementation contains the #ifdefs to change the implementation.
 * Since Windows is usually different that is usually the first case, after
 * that the behaviour is usually Unix/BSD-like with occasional variation.
 */

#include "../../stdafx.h"
#include "os_abstraction.h"
#include "../../string_func.h"

#if defined(_WIN32)
#include "../../core/format.hpp"
#endif

#include <mutex>

#include "../../safeguards.h"

/**
 * Construct the network error with the given error code.
 * @param error The error code.
 */
NetworkError::NetworkError(int error) : error(error)
{
}

/**
 * Check whether this error describes that the operation would block.
 * @return True iff the operation would block.
 */
bool NetworkError::WouldBlock() const
{
#if defined(_WIN32)
	return this->error == WSAEWOULDBLOCK;
#else
	/* Usually EWOULDBLOCK and EAGAIN are the same, but sometimes they are not
	 * and the POSIX.1 specification states that either should be checked. */
	return this->error == EWOULDBLOCK || this->error == EAGAIN;
#endif
}

/**
 * Check whether this error describes a connection reset.
 * @return True iff the connection is reset.
 */
bool NetworkError::IsConnectionReset() const
{
#if defined(_WIN32)
	return this->error == WSAECONNRESET;
#else
	return this->error == ECONNRESET;
#endif
}

/**
 * Check whether this error describes a connect is in progress.
 * @return True iff the connect is already in progress.
 */
bool NetworkError::IsConnectInProgress() const
{
#if defined(_WIN32)
	return this->error == WSAEWOULDBLOCK;
#else
	return this->error == EINPROGRESS;
#endif
}

/**
 * Get the string representation of the error message.
 * @return The string representation that will get overwritten by next calls.
 */
const char *NetworkError::AsString() const
{
	if (this->message.empty()) {
#if defined(_WIN32)
		wchar_t buffer[512];
		if (FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, this->error,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), buffer, static_cast<DWORD>(std::size(buffer)), nullptr) == 0) {
			this->message = fmt::format("Unknown error {}", this->error);
		} else {
			this->message = FS2OTTD(buffer);
		}
#else
		this->message.assign(StrErrorDumper().Get(this->error));
#endif
	}
	return this->message.c_str();
}

/**
 * Check whether an error was actually set.
 * @return True iff an error was set.
 */
bool NetworkError::HasError() const
{
	return this->error != 0;
}

/**
 * Get the last network error.
 * @return The network error.
 */
/* static */ NetworkError NetworkError::GetLast()
{
#if defined(_WIN32)
	return NetworkError(WSAGetLastError());
#else
	return NetworkError(errno);
#endif
}


/**
 * Try to set the socket into non-blocking mode.
 * @param d The socket to set the non-blocking more for.
 * @return True if setting the non-blocking mode succeeded, otherwise false.
 */
bool SetNonBlocking([[maybe_unused]] SOCKET d)
{
#if defined(_WIN32)
	u_long nonblocking = 1;
	return ioctlsocket(d, FIONBIO, &nonblocking) == 0;
#elif defined __EMSCRIPTEN__
	return true;
#else
	int nonblocking = 1;
	return ioctl(d, FIONBIO, &nonblocking) == 0;
#endif
}

/**
 * Try to set the socket into blocking mode.
 * @param d The socket to set the blocking more for.
 * @return True if setting the blocking mode succeeded, otherwise false.
 */
bool SetBlocking(SOCKET d)
{
#if defined(_WIN32)
	u_long nonblocking = 0;
	return ioctlsocket(d, FIONBIO, &nonblocking) == 0;
#elif defined __EMSCRIPTEN__
	return true;
#else
	int nonblocking = 0;
	return ioctl(d, FIONBIO, &nonblocking) == 0;
#endif
}

/**
 * Try to set the socket to not delay sending.
 * @param d The socket to disable the delaying for.
 * @return True if disabling the delaying succeeded, otherwise false.
 */
bool SetNoDelay([[maybe_unused]] SOCKET d)
{
#ifdef __EMSCRIPTEN__
	return true;
#else
	int flags = 1;
	/* The (const char*) cast is needed for windows */
	return setsockopt(d, IPPROTO_TCP, TCP_NODELAY, (const char *)&flags, sizeof(flags)) == 0;
#endif
}

/**
 * Try to set the socket to reuse ports.
 * @param d The socket to reuse ports on.
 * @return True if disabling the delaying succeeded, otherwise false.
 */
bool SetReusePort(SOCKET d)
{
#ifdef _WIN32
	/* Windows has no SO_REUSEPORT, but for our usecases SO_REUSEADDR does the same job. */
	int reuse_port = 1;
	return setsockopt(d, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse_port, sizeof(reuse_port)) == 0;
#else
	int reuse_port = 1;
	return setsockopt(d, SOL_SOCKET, SO_REUSEPORT, &reuse_port, sizeof(reuse_port)) == 0;
#endif
}

/**
 * Try to shutdown the socket in one or both directions.
 * @param d The socket to disable the delaying for.
 * @param read Whether to shutdown the read direction.
 * @param write Whether to shutdown the write direction.
 * @param linger_timeout The socket linger timeout.
 * @return True if successful
 */
bool ShutdownSocket(SOCKET d, bool read, bool write, uint linger_timeout)
{
	if (!read && !write) return true;
#ifdef _WIN32
	LINGER ln = { 1U, (uint16_t) linger_timeout };
#else
	struct linger ln = { 1, (int) linger_timeout };
#endif

	setsockopt(d, SOL_SOCKET, SO_LINGER, (const char*)&ln, sizeof(ln));

	int how = SD_BOTH;
	if (!read) how = SD_SEND;
	if (!write) how = SD_RECEIVE;
	return shutdown(d, how) == 0;
}

/**
 * Get the error from a socket, if any.
 * @param d The socket to get the error from.
 * @return The errno on the socket.
 */
NetworkError GetSocketError(SOCKET d)
{
	int err;
	socklen_t len = sizeof(err);
	getsockopt(d, SOL_SOCKET, SO_ERROR, (char *)&err, &len);

	return NetworkError(err);
}
