/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * Copyright (c) 2024 BubiCam Contributors
 * MIT License
 *
 * StreamServer - MJPEG over HTTP streaming server
 */

#include "StreamServer.h"

#define LOG_MODULE "StreamServer"
#include "ErrorUtils.h"

#include <Autolock.h>
#include <OS.h>

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <jpeglib.h>

static const char* kBoundary = "bubicam_stream_boundary";

static const char* kHTTPStreamResponse =
	"HTTP/1.1 200 OK\r\n"
	"Content-Type: multipart/x-mixed-replace; boundary=%s\r\n"
	"Cache-Control: no-cache, no-store\r\n"
	"Pragma: no-cache\r\n"
	"Access-Control-Allow-Origin: *\r\n"
	"Connection: close\r\n"
	"\r\n";

static const char* kHTTPSnapshotResponse =
	"HTTP/1.1 200 OK\r\n"
	"Content-Type: image/jpeg\r\n"
	"Content-Length: %lu\r\n"
	"Cache-Control: no-cache\r\n"
	"Access-Control-Allow-Origin: *\r\n"
	"Connection: close\r\n"
	"\r\n";

static const char* kHTTPIndexPage =
	"HTTP/1.1 200 OK\r\n"
	"Content-Type: text/html\r\n"
	"Connection: close\r\n"
	"\r\n"
	"<!DOCTYPE html><html><head><title>BubiCam Stream</title>"
	"<style>body{background:#1a1a1a;display:flex;justify-content:center;"
	"align-items:center;min-height:100vh;margin:0;font-family:sans-serif}"
	"img{max-width:100%%;max-height:90vh;border:2px solid #444;border-radius:4px}"
	"h1{color:#eee;text-align:center;font-size:14px;margin:8px 0}"
	".c{display:flex;flex-direction:column;align-items:center}</style></head>"
	"<body><div class='c'><h1>BubiCam Live Stream</h1>"
	"<img src='/stream' alt='Live stream'></div></body></html>";

static const char* kHTTP404 =
	"HTTP/1.1 404 Not Found\r\n"
	"Content-Type: text/plain\r\n"
	"Content-Length: 9\r\n"
	"Connection: close\r\n"
	"\r\n"
	"Not Found";

static const char* kHTTP503 =
	"HTTP/1.1 503 Service Unavailable\r\n"
	"Content-Type: text/plain\r\n"
	"Content-Length: 19\r\n"
	"Connection: close\r\n"
	"\r\n"
	"Too many clients\r\n";


StreamServer::StreamServer(BMessenger target)
	:
	fTarget(target),
	fRunning(false),
	fPort(0),
	fServerSocket(-1),
	fListenerThread(-1),
	fCurrentJPEG(NULL),
	fCurrentJPEGSize(0),
	fFrameSequence(0),
	fFrameLock("stream frame"),
	fLastFeedTime(0),
	fMinFrameInterval(33333),  // ~30 fps default
	fClientLock("stream clients"),
	fClientCount(0),
	fMaxClients(4),
	fJPEGQuality(70),
	fFramesServed(0)
{
}


StreamServer::~StreamServer()
{
	Stop();
	free(fCurrentJPEG);
}


status_t
StreamServer::Start(uint16 port)
{
	if (fRunning)
		return B_BUSY;

	fServerSocket = socket(AF_INET, SOCK_STREAM, 0);
	if (fServerSocket < 0) {
		LOG_ERROR("Failed to create socket: %s", strerror(errno));
		return B_ERROR;
	}

	int reuse = 1;
	setsockopt(fServerSocket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(port);

	if (bind(fServerSocket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
		LOG_ERROR("Bind failed on port %d: %s", port, strerror(errno));
		close(fServerSocket);
		fServerSocket = -1;
		return B_ERROR;
	}

	if (listen(fServerSocket, 8) < 0) {
		LOG_ERROR("Listen failed: %s", strerror(errno));
		close(fServerSocket);
		fServerSocket = -1;
		return B_ERROR;
	}

	fPort = port;
	fRunning = true;
	fFramesServed = 0;

	fListenerThread = spawn_thread(_ListenerThread, "stream_listener",
		B_NORMAL_PRIORITY, this);
	if (fListenerThread < 0) {
		LOG_ERROR("Failed to spawn listener thread");
		close(fServerSocket);
		fServerSocket = -1;
		fRunning = false;
		return B_ERROR;
	}

	resume_thread(fListenerThread);

	LOG_INFO("Stream server started on port %d", port);

	BMessage msg(MSG_STREAM_STARTED);
	msg.AddInt16("port", port);
	fTarget.SendMessage(&msg);

	return B_OK;
}


void
StreamServer::Stop()
{
	if (!fRunning)
		return;

	fRunning = false;

	// Close server socket to unblock accept()
	if (fServerSocket >= 0) {
		close(fServerSocket);
		fServerSocket = -1;
	}

	// Wait for listener thread
	if (fListenerThread >= 0) {
		status_t exitValue;
		wait_for_thread(fListenerThread, &exitValue);
		fListenerThread = -1;
	}

	// Close all client connections
	{
		BAutolock lock(fClientLock);
		for (int32 i = 0; i < fClients.CountItems(); i++) {
			ClientInfo* client = fClients.ItemAt(i);
			client->active = false;
			if (client->socket >= 0)
				close(client->socket);
		}
	}

	// Wait for client threads to exit
	{
		BAutolock lock(fClientLock);
		for (int32 i = 0; i < fClients.CountItems(); i++) {
			ClientInfo* client = fClients.ItemAt(i);
			if (client->thread >= 0) {
				status_t exitValue;
				lock.Unlock();
				wait_for_thread(client->thread, &exitValue);
				lock.Lock();
			}
		}
		fClients.MakeEmpty(true);
		fClientCount = 0;
	}

	LOG_INFO("Stream server stopped");

	fTarget.SendMessage(MSG_STREAM_STOPPED);
}


void
StreamServer::FeedFrame(BBitmap* bitmap)
{
	if (!fRunning || bitmap == NULL || fClientCount == 0)
		return;

	// Frame rate throttle
	bigtime_t now = system_time();
	if (now - fLastFeedTime < fMinFrameInterval)
		return;
	fLastFeedTime = now;

	// Compress to JPEG
	uint8* jpegData = NULL;
	unsigned long jpegSize = 0;

	status_t status = _CompressJPEG(bitmap, &jpegData, &jpegSize);
	if (status != B_OK || jpegData == NULL)
		return;

	// Swap into shared frame buffer
	{
		BAutolock lock(fFrameLock);
		free(fCurrentJPEG);
		fCurrentJPEG = jpegData;
		fCurrentJPEGSize = jpegSize;
		fFrameSequence++;
	}
}


void
StreamServer::SetJPEGQuality(int quality)
{
	if (quality < 10) quality = 10;
	if (quality > 100) quality = 100;
	fJPEGQuality = quality;
}


void
StreamServer::SetMaxFPS(float fps)
{
	if (fps < 1.0f) fps = 1.0f;
	if (fps > 60.0f) fps = 60.0f;
	fMinFrameInterval = (bigtime_t)(1000000.0f / fps);
}


void
StreamServer::SetMaxClients(int32 max)
{
	if (max < 1) max = 1;
	if (max > 16) max = 16;
	fMaxClients = max;
}


// ============================================================================
// Listener thread - accepts incoming connections
// ============================================================================

int32
StreamServer::_ListenerThread(void* data)
{
	StreamServer* server = static_cast<StreamServer*>(data);

	while (server->fRunning) {
		struct sockaddr_in clientAddr;
		socklen_t clientLen = sizeof(clientAddr);

		int clientSocket = accept(server->fServerSocket,
			(struct sockaddr*)&clientAddr, &clientLen);

		if (clientSocket < 0) {
			if (server->fRunning)
				LOG_ERROR("Accept failed: %s", strerror(errno));
			break;
		}

		// Set send timeout to prevent blocking on slow clients
		struct timeval tv;
		tv.tv_sec = 2;
		tv.tv_usec = 0;
		setsockopt(clientSocket, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

		// Read the HTTP request
		char buffer[2048];
		ssize_t bytesRead = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
		if (bytesRead <= 0) {
			close(clientSocket);
			continue;
		}
		buffer[bytesRead] = '\0';

		// Parse request path
		char method[16] = {0};
		char path[256] = {0};
		sscanf(buffer, "%15s %255s", method, path);

		// Route requests
		if (strcmp(path, "/") == 0) {
			// Serve HTML page with embedded stream
			send(clientSocket, kHTTPIndexPage, strlen(kHTTPIndexPage), 0);
			close(clientSocket);
			continue;
		}

		if (strcmp(path, "/snapshot") == 0) {
			// Single JPEG snapshot
			BAutolock lock(server->fFrameLock);
			if (server->fCurrentJPEG != NULL && server->fCurrentJPEGSize > 0) {
				char header[256];
				snprintf(header, sizeof(header), kHTTPSnapshotResponse,
					server->fCurrentJPEGSize);
				send(clientSocket, header, strlen(header), 0);
				send(clientSocket, server->fCurrentJPEG,
					server->fCurrentJPEGSize, 0);
			} else {
				const char* noFrame =
					"HTTP/1.1 503 Service Unavailable\r\n"
					"Content-Type: text/plain\r\n"
					"Content-Length: 18\r\n"
					"Connection: close\r\n"
					"\r\n"
					"No frame available";
				send(clientSocket, noFrame, strlen(noFrame), 0);
			}
			close(clientSocket);
			continue;
		}

		if (strcmp(path, "/stream") != 0) {
			send(clientSocket, kHTTP404, strlen(kHTTP404), 0);
			close(clientSocket);
			continue;
		}

		// Check client limit
		server->_RemoveDeadClients();
		if (server->fClientCount >= server->fMaxClients) {
			send(clientSocket, kHTTP503, strlen(kHTTP503), 0);
			close(clientSocket);
			continue;
		}

		// Create client thread for streaming
		ClientInfo* client = new ClientInfo();
		client->socket = clientSocket;
		client->active = true;

		// Pack server + client into a struct for the thread
		struct ClientData {
			StreamServer*	server;
			ClientInfo*		client;
		};
		ClientData* cd = new ClientData();
		cd->server = server;
		cd->client = client;

		client->thread = spawn_thread(_ClientThread, "stream_client",
			B_LOW_PRIORITY, cd);

		if (client->thread < 0) {
			close(clientSocket);
			delete cd;
			delete client;
			continue;
		}

		{
			BAutolock lock(server->fClientLock);
			server->fClients.AddItem(client);
			server->fClientCount++;
		}

		char addrStr[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &clientAddr.sin_addr, addrStr, sizeof(addrStr));
		LOG_INFO("Stream client connected from %s (%d active)",
			addrStr, (int)server->fClientCount.load());

		BMessage msg(MSG_STREAM_CLIENT);
		msg.AddInt32("count", server->fClientCount);
		server->fTarget.SendMessage(&msg);

		resume_thread(client->thread);
	}

	return 0;
}


// ============================================================================
// Client thread - sends MJPEG stream to one client
// ============================================================================

int32
StreamServer::_ClientThread(void* data)
{
	struct ClientData {
		StreamServer*			server;
		StreamServer::ClientInfo*	client;
	};
	ClientData* cd = static_cast<ClientData*>(data);
	StreamServer* server = cd->server;
	StreamServer::ClientInfo* client = cd->client;
	delete cd;

	int sock = client->socket;

	// Send HTTP response header
	char header[256];
	snprintf(header, sizeof(header), kHTTPStreamResponse, kBoundary);
	if (send(sock, header, strlen(header), 0) < 0) {
		client->active = false;
		close(sock);
		server->fClientCount--;
		return -1;
	}

	uint32 lastSequence = 0;

	while (client->active && server->fRunning) {
		// Wait for a new frame
		uint8* jpegCopy = NULL;
		unsigned long jpegSize = 0;

		{
			BAutolock lock(server->fFrameLock);
			if (server->fFrameSequence == lastSequence
				|| server->fCurrentJPEG == NULL) {
				lock.Unlock();
				snooze(10000);  // 10ms poll
				continue;
			}

			// Copy the JPEG data so we don't hold the lock during send
			jpegSize = server->fCurrentJPEGSize;
			jpegCopy = (uint8*)malloc(jpegSize);
			if (jpegCopy == NULL) {
				lock.Unlock();
				snooze(50000);
				continue;
			}
			memcpy(jpegCopy, server->fCurrentJPEG, jpegSize);
			lastSequence = server->fFrameSequence;
		}

		// Build multipart frame header
		char partHeader[256];
		int headerLen = snprintf(partHeader, sizeof(partHeader),
			"--%s\r\n"
			"Content-Type: image/jpeg\r\n"
			"Content-Length: %lu\r\n"
			"\r\n",
			kBoundary, jpegSize);

		// Send header + JPEG data + trailing CRLF
		bool sendOK = true;
		if (send(sock, partHeader, headerLen, 0) < 0)
			sendOK = false;
		if (sendOK && send(sock, jpegCopy, jpegSize, 0) < 0)
			sendOK = false;
		if (sendOK && send(sock, "\r\n", 2, 0) < 0)
			sendOK = false;

		free(jpegCopy);

		if (!sendOK) {
			LOG_DEBUG("Client disconnected (send failed)");
			break;
		}

		server->fFramesServed++;
	}

	client->active = false;
	close(sock);
	client->socket = -1;
	server->fClientCount--;

	LOG_INFO("Stream client disconnected (%d remaining)",
		(int)server->fClientCount.load());

	BMessage msg(MSG_STREAM_CLIENT);
	msg.AddInt32("count", server->fClientCount);
	server->fTarget.SendMessage(&msg);

	return 0;
}


// ============================================================================
// JPEG compression (adapted from VideoRecorder)
// ============================================================================

status_t
StreamServer::_CompressJPEG(BBitmap* bitmap, uint8** outData,
	unsigned long* outSize)
{
	if (bitmap == NULL || !bitmap->IsValid())
		return B_BAD_VALUE;

	*outData = NULL;
	*outSize = 0;

	struct jpeg_compress_struct cinfo;
	struct jpeg_error_mgr jerr;

	cinfo.err = jpeg_std_error(&jerr);

	jmp_buf jumpBuffer;
	cinfo.client_data = &jumpBuffer;
	jerr.error_exit = [](j_common_ptr cinfo) {
		jmp_buf* jb = static_cast<jmp_buf*>(cinfo->client_data);
		longjmp(*jb, 1);
	};

	if (setjmp(jumpBuffer)) {
		jpeg_destroy_compress(&cinfo);
		if (*outData != NULL) {
			free(*outData);
			*outData = NULL;
		}
		return B_ERROR;
	}

	jpeg_create_compress(&cinfo);
	jpeg_mem_dest(&cinfo, outData, outSize);

	cinfo.image_width = bitmap->Bounds().IntegerWidth() + 1;
	cinfo.image_height = bitmap->Bounds().IntegerHeight() + 1;
	cinfo.input_components = 3;
	cinfo.in_color_space = JCS_RGB;

	jpeg_set_defaults(&cinfo);
	jpeg_set_quality(&cinfo, fJPEGQuality, TRUE);
	jpeg_start_compress(&cinfo, TRUE);

	int32 width = cinfo.image_width;
	int32 srcBytesPerRow = bitmap->BytesPerRow();
	const uint8* srcBits = static_cast<const uint8*>(bitmap->Bits());

	uint8* rowBuffer = new uint8[width * 3];

	while (cinfo.next_scanline < cinfo.image_height) {
		const uint8* srcRow = srcBits + cinfo.next_scanline * srcBytesPerRow;
		for (int32 x = 0; x < width; x++) {
			rowBuffer[x * 3 + 0] = srcRow[x * 4 + 2];  // R
			rowBuffer[x * 3 + 1] = srcRow[x * 4 + 1];  // G
			rowBuffer[x * 3 + 2] = srcRow[x * 4 + 0];  // B
		}
		JSAMPROW row = rowBuffer;
		jpeg_write_scanlines(&cinfo, &row, 1);
	}

	delete[] rowBuffer;

	jpeg_finish_compress(&cinfo);
	jpeg_destroy_compress(&cinfo);

	return B_OK;
}


void
StreamServer::_RemoveDeadClients()
{
	BAutolock lock(fClientLock);

	for (int32 i = fClients.CountItems() - 1; i >= 0; i--) {
		ClientInfo* client = fClients.ItemAt(i);
		if (!client->active) {
			if (client->thread >= 0) {
				status_t exitValue;
				lock.Unlock();
				wait_for_thread(client->thread, &exitValue);
				lock.Lock();
			}
			fClients.RemoveItemAt(i);
			delete client;
		}
	}
}
