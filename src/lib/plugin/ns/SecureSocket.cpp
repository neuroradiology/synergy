/*
 * synergy -- mouse and keyboard sharing utility
 * Copyright (C) 2015 Synergy Si Ltd.
 * 
 * This package is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * found in the file COPYING that should have accompanied this file.
 * 
 * This package is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "SecureSocket.h"

#include "net/TSocketMultiplexerMethodJob.h"
#include "net/TCPSocket.h"
#include "mt/Lock.h"
#include "arch/XArch.h"
#include "base/Log.h"

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <cstring>
#include <cstdlib>
#include <memory>

//
// SecureSocket
//

#define MAX_ERROR_SIZE 65535

struct Ssl {
	SSL_CTX*	m_context;
	SSL*		m_ssl;
};

SecureSocket::SecureSocket(
		IEventQueue* events,
		SocketMultiplexer* socketMultiplexer) :
	TCPSocket(events, socketMultiplexer),
	m_secureReady(false)
{
}

SecureSocket::SecureSocket(
		IEventQueue* events,
		SocketMultiplexer* socketMultiplexer,
		ArchSocket socket) :
	TCPSocket(events, socketMultiplexer, socket),
	m_secureReady(false)
{
}

SecureSocket::~SecureSocket()
{
	if (m_ssl->m_ssl != NULL) {
		SSL_shutdown(m_ssl->m_ssl);

		SSL_free(m_ssl->m_ssl);
		m_ssl->m_ssl = NULL;
	}
	if (m_ssl->m_context != NULL) {
		SSL_CTX_free(m_ssl->m_context);
		m_ssl->m_context = NULL;
	}

	delete m_ssl;
}

void
SecureSocket::close()
{
	SSL_shutdown(m_ssl->m_ssl);

	TCPSocket::close();
}

void
SecureSocket::secureConnect()
{
	setJob(new TSocketMultiplexerMethodJob<SecureSocket>(
			this, &SecureSocket::serviceConnect,
			getSocket(), isReadable(), isWritable()));
}

void
SecureSocket::secureAccept()
{
	setJob(new TSocketMultiplexerMethodJob<SecureSocket>(
			this, &SecureSocket::serviceAccept,
			getSocket(), isReadable(), isWritable()));
}

UInt32
SecureSocket::secureRead(void* buffer, UInt32 n)
{
	int r = 0;
	if (m_ssl->m_ssl != NULL) {
		r = SSL_read(m_ssl->m_ssl, buffer, n);
		
		bool fatal, retry;
		checkResult(r, fatal, retry);
		
		if (retry) {
			r = 0;
		}
	}

	return r > 0 ? (UInt32)r : 0;
}

UInt32
SecureSocket::secureWrite(const void* buffer, UInt32 n)
{
	int r = 0;
	if (m_ssl->m_ssl != NULL) {
		r = SSL_write(m_ssl->m_ssl, buffer, n);
		
		bool fatal, retry;
		checkResult(r, fatal, retry);

		if (retry) {
			r = 0;
		}
	}

	return r > 0 ? (UInt32)r : 0;
}

bool
SecureSocket::isSecureReady()
{
	return m_secureReady;
}

void
SecureSocket::initSsl(bool server)
{
	m_ssl = new Ssl();
	m_ssl->m_context = NULL;
	m_ssl->m_ssl = NULL;

	initContext(server);
}

void
SecureSocket::loadCertificates(const char* filename)
{
	int r = 0;
	r = SSL_CTX_use_certificate_file(m_ssl->m_context, filename, SSL_FILETYPE_PEM);
	if (r <= 0) {
		throwError("could not use ssl certificate");
	}

	r = SSL_CTX_use_PrivateKey_file(m_ssl->m_context, filename, SSL_FILETYPE_PEM);
	if (r <= 0) {
		throwError("could not use ssl private key");
	}

	r = SSL_CTX_check_private_key(m_ssl->m_context);
	if (!r) {
		throwError("could not verify ssl private key");
	}
}

void
SecureSocket::initContext(bool server)
{
	SSL_library_init();

	const SSL_METHOD* method;
 
	// load & register all cryptos, etc.
	OpenSSL_add_all_algorithms();

	// load all error messages
	SSL_load_error_strings();

	if (server) {
		// create new server-method instance
		method = SSLv23_server_method();
	}
	else {
		// create new client-method instance
		method = SSLv23_client_method();
	}
	
	// create new context from method
	SSL_METHOD* m = const_cast<SSL_METHOD*>(method);
	m_ssl->m_context = SSL_CTX_new(m);
	if (m_ssl->m_context == NULL) {
		showError();
	}
}

void
SecureSocket::createSSL()
{
	// I assume just one instance is needed
	// get new SSL state with context
	if (m_ssl->m_ssl == NULL) {
		m_ssl->m_ssl = SSL_new(m_ssl->m_context);
	}
}

bool
SecureSocket::secureAccept(int socket)
{
	createSSL();

	// set connection socket to SSL state
	SSL_set_fd(m_ssl->m_ssl, socket);
	
	LOG((CLOG_DEBUG2 "accepting secure socket"));
	int r = SSL_accept(m_ssl->m_ssl);
	
	bool fatal, retry;
	checkResult(r, fatal, retry);

	if (fatal) {
		// tell user and sleep so the socket isn't hammered.
		LOG((CLOG_ERR "failed to accept secure socket"));
		LOG((CLOG_INFO "client connection may not be secure"));
		ARCH->sleep(1);
	}

	m_secureReady = !retry;
	if (m_secureReady) {
		LOG((CLOG_INFO "accepted secure socket"));
	}

	return retry;
}

bool
SecureSocket::secureConnect(int socket)
{
	createSSL();

	// attach the socket descriptor
	SSL_set_fd(m_ssl->m_ssl, socket);
	
	LOG((CLOG_DEBUG2 "connecting secure socket"));
	int r = SSL_connect(m_ssl->m_ssl);
	
	bool fatal, retry;
	checkResult(r, fatal, retry);

	if (fatal) {
		// tell user and sleep so the socket isn't hammered.
		LOG((CLOG_ERR "failed to connect secure socket"));
		LOG((CLOG_INFO "server connection may not be secure"));
		ARCH->sleep(1);
	}

	m_secureReady = !retry;

	if (m_secureReady) {
		LOG((CLOG_INFO "connected to secure socket"));
		showCertificate();
	}

	return retry;
}

void
SecureSocket::showCertificate()
{
	X509* cert;
	char* line;
 
	// get the server's certificate
	cert = SSL_get_peer_certificate(m_ssl->m_ssl);
	if (cert != NULL) {
		line = X509_NAME_oneline(X509_get_subject_name(cert), 0, 0);
		LOG((CLOG_INFO "server ssl certificate info: %s", line));
		OPENSSL_free(line);
		X509_free(cert);
	}
	else {
		throwError("server has no ssl certificate");
	}
}

void
SecureSocket::checkResult(int n, bool& fatal, bool& retry)
{
	// ssl errors are a little quirky. the "want" errors are normal and
	// should result in a retry.

	fatal = false;
	retry = false;

	int errorCode = SSL_get_error(m_ssl->m_ssl, n);
	switch (errorCode) {
	case SSL_ERROR_NONE:
		// operation completed
		break;

	case SSL_ERROR_ZERO_RETURN:
		// connection closed
		LOG((CLOG_DEBUG2 "secure socket error: SSL_ERROR_ZERO_RETURN"));
		break;

	case SSL_ERROR_WANT_READ:
		LOG((CLOG_DEBUG2 "secure socket error: SSL_ERROR_WANT_READ"));
		retry = true;
		break;

	case SSL_ERROR_WANT_WRITE:
		LOG((CLOG_DEBUG2 "secure socket error: SSL_ERROR_WANT_WRITE"));
		retry = true;
		break;

	case SSL_ERROR_WANT_CONNECT:
		LOG((CLOG_DEBUG2 "secure socket error: SSL_ERROR_WANT_CONNECT"));
		retry = true;
		break;

	case SSL_ERROR_WANT_ACCEPT:
		LOG((CLOG_DEBUG2 "secure socket error: SSL_ERROR_WANT_ACCEPT"));
		retry = true;
		break;

	case SSL_ERROR_SYSCALL:
		LOG((CLOG_ERR "secure socket error: SSL_ERROR_SYSCALL"));
		fatal = true;
		break;

	case SSL_ERROR_SSL:
		LOG((CLOG_ERR "secure socket error: SSL_ERROR_SSL"));
		fatal = true;
		break;

	default:
		LOG((CLOG_ERR "secure socket error: SSL_ERROR_SSL"));
		fatal = true;
		break;
	}

	if (fatal) {
		showError();
		sendEvent(getEvents()->forISocket().disconnected());
		sendEvent(getEvents()->forIStream().inputShutdown());
	}
}

void
SecureSocket::showError()
{
	String error = getError();
	if (!error.empty()) {
		LOG((CLOG_ERR "secure socket error: %s", error.c_str()));
	}
}

void
SecureSocket::throwError(const char* reason)
{
	String error = getError();
	if (!error.empty()) {
		throw XSocket(synergy::string::sprintf(
			"%s: %s", reason, error.c_str()));
	}
	else {
		throw XSocket(reason);
	}
}

String
SecureSocket::getError()
{
	unsigned long e = ERR_get_error();

	if (e != 0) {
		char error[MAX_ERROR_SIZE];
		ERR_error_string_n(e, error, MAX_ERROR_SIZE);
		return error;
	}
	else {
		return "";
	}
}

ISocketMultiplexerJob*
SecureSocket::serviceConnect(ISocketMultiplexerJob* job,
				bool, bool write, bool error)
{
	Lock lock(&getMutex());

	bool retry = true;
#ifdef SYSAPI_WIN32
	retry = secureConnect(static_cast<int>(getSocket()->m_socket));
#elif SYSAPI_UNIX
	retry = secureConnect(getSocket()->m_fd);
#endif

	return retry ? job : newJob();
}

ISocketMultiplexerJob*
SecureSocket::serviceAccept(ISocketMultiplexerJob* job,
				bool, bool write, bool error)
{
	Lock lock(&getMutex());

	bool retry = true;
#ifdef SYSAPI_WIN32
	retry = secureAccept(static_cast<int>(getSocket()->m_socket));
#elif SYSAPI_UNIX
	retry = secureAccept(getSocket()->m_fd);
#endif

	return retry ? job : newJob();
}
