/*
 * MCPServer.cpp - Model Context Protocol server implementation
 */

#include "MCPServer.h"
#include "WebcamDevice.h"

// Logging macros using centralized ErrorUtils
#define LOG_MODULE "MCPServer"
#include "ErrorUtils.h"

#include <Application.h>
#include <Autolock.h>
#include <Bitmap.h>
#include <BitmapStream.h>
#include <File.h>
#include <FindDirectory.h>
#include <Path.h>
#include <TranslatorRoster.h>

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

// Base64 encoding table
static const char kBase64Table[] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";


static BString
Base64Encode(const uint8* data, size_t length)
{
	BString result;
	size_t i;

	for (i = 0; i + 2 < length; i += 3) {
		result += kBase64Table[(data[i] >> 2) & 0x3F];
		result += kBase64Table[((data[i] & 0x3) << 4) | ((data[i + 1] >> 4) & 0xF)];
		result += kBase64Table[((data[i + 1] & 0xF) << 2) | ((data[i + 2] >> 6) & 0x3)];
		result += kBase64Table[data[i + 2] & 0x3F];
	}

	if (i < length) {
		result += kBase64Table[(data[i] >> 2) & 0x3F];
		if (i + 1 < length) {
			result += kBase64Table[((data[i] & 0x3) << 4) | ((data[i + 1] >> 4) & 0xF)];
			result += kBase64Table[(data[i + 1] & 0xF) << 2];
		} else {
			result += kBase64Table[(data[i] & 0x3) << 4];
			result += '=';
		}
		result += '=';
	}

	return result;
}


MCPServer::MCPServer(BMessenger target)
	:
	BLooper("MCP Server"),
	fTarget(target),
	fWebcamDevice(NULL),
	fTools(10),
	fRunning(false),
	fPort(9847),
	fListenerThread(-1),
	fServerSocket(-1),
	fConnectedClients(0),
	fTotalRequests(0),
	fTotalErrors(0)
{
	// Register available tools
	// Note: fTools is BObjectList<MCPTool, true> - owns items, handles memory
	MCPTool* tool;

	tool = new MCPTool();
	tool->name = "capture_frame";
	tool->description = "Capture a frame from the webcam and return it as base64 PNG";
	tool->inputSchema = "{\"type\":\"object\",\"properties\":{\"format\":{\"type\":\"string\",\"enum\":[\"png\",\"jpeg\"],\"default\":\"png\"}}}";
	fTools.AddItem(tool);

	tool = new MCPTool();
	tool->name = "get_driver_info";
	tool->description = "Get information about the webcam driver";
	tool->inputSchema = "{\"type\":\"object\",\"properties\":{}}";
	fTools.AddItem(tool);

	tool = new MCPTool();
	tool->name = "set_resolution";
	tool->description = "Set the webcam resolution";
	tool->inputSchema = "{\"type\":\"object\",\"properties\":{\"width\":{\"type\":\"integer\"},\"height\":{\"type\":\"integer\"}},\"required\":[\"width\",\"height\"]}";
	fTools.AddItem(tool);

	tool = new MCPTool();
	tool->name = "get_status";
	tool->description = "Get current webcam status (resolution, fps, streaming state)";
	tool->inputSchema = "{\"type\":\"object\",\"properties\":{}}";
	fTools.AddItem(tool);

	tool = new MCPTool();
	tool->name = "get_supported_formats";
	tool->description = "Get list of supported video formats/resolutions";
	tool->inputSchema = "{\"type\":\"object\",\"properties\":{}}";
	fTools.AddItem(tool);

	Run();
}


MCPServer::~MCPServer()
{
	Stop();
}


status_t
MCPServer::Start(uint16 port)
{
	if (fRunning)
		return B_BUSY;

	fPort = port;

	// Create socket
	fServerSocket = socket(AF_INET, SOCK_STREAM, 0);
	if (fServerSocket < 0) {
		_Log("Failed to create socket: %s", strerror(errno));
		return B_ERROR;
	}

	// Allow address reuse
	int opt = 1;
	setsockopt(fServerSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	// Bind to port
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(port);

	if (bind(fServerSocket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
		_Log("Failed to bind to port %d: %s", port, strerror(errno));
		close(fServerSocket);
		fServerSocket = -1;
		return B_ERROR;
	}

	// Listen
	if (listen(fServerSocket, 5) < 0) {
		_Log("Failed to listen: %s", strerror(errno));
		close(fServerSocket);
		fServerSocket = -1;
		return B_ERROR;
	}

	fRunning = true;

	// Start listener thread
	fListenerThread = spawn_thread(_ListenerThread, "MCP Listener",
		B_NORMAL_PRIORITY, this);
	if (fListenerThread < 0) {
		_Log("Failed to spawn listener thread");
		close(fServerSocket);
		fServerSocket = -1;
		fRunning = false;
		return B_ERROR;
	}

	resume_thread(fListenerThread);

	_Log("MCP Server started on port %d", port);

	// Notify target
	BMessage msg(MCP_SERVER_STATUS);
	msg.AddBool("running", true);
	msg.AddInt16("port", port);
	fTarget.SendMessage(&msg);

	return B_OK;
}


status_t
MCPServer::Stop()
{
	if (!fRunning)
		return B_OK;

	fRunning = false;

	// Close server socket to unblock accept()
	if (fServerSocket >= 0) {
		close(fServerSocket);
		fServerSocket = -1;
	}

	// Wait for listener thread
	if (fListenerThread >= 0) {
		status_t status;
		wait_for_thread(fListenerThread, &status);
		fListenerThread = -1;
	}

	_Log("MCP Server stopped");

	// Notify target
	BMessage msg(MCP_SERVER_STATUS);
	msg.AddBool("running", false);
	fTarget.SendMessage(&msg);

	return B_OK;
}


MCPServerStats
MCPServer::GetStats() const
{
	MCPServerStats stats;
	stats.running = fRunning;
	stats.port = fPort;
	stats.connectedClients = fConnectedClients;
	stats.totalRequests = fTotalRequests;
	stats.totalErrors = fTotalErrors;
	return stats;
}


void
MCPServer::SetWebcamDevice(WebcamDevice* device)
{
	BAutolock lock(fDeviceLock);
	fWebcamDevice = device;
}


WebcamDevice*
MCPServer::_LockDevice()
{
	fDeviceLock.Lock();
	if (fWebcamDevice == NULL) {
		fDeviceLock.Unlock();
		return NULL;
	}
	return fWebcamDevice;
}


void
MCPServer::_UnlockDevice()
{
	fDeviceLock.Unlock();
}


void
MCPServer::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case MCP_SERVER_START:
		{
			uint16 port = 9847;
			message->FindInt16("port", (int16*)&port);
			Start(port);
			break;
		}

		case MCP_SERVER_STOP:
			Stop();
			break;

		default:
			BLooper::MessageReceived(message);
	}
}


int32
MCPServer::_ListenerThread(void* data)
{
	MCPServer* server = (MCPServer*)data;

	while (server->fRunning) {
		struct sockaddr_in clientAddr;
		socklen_t clientLen = sizeof(clientAddr);

		int clientSocket = accept(server->fServerSocket,
			(struct sockaddr*)&clientAddr, &clientLen);

		if (clientSocket < 0) {
			if (server->fRunning)
				server->_Log("Accept failed: %s", strerror(errno));
			continue;
		}

		++server->fConnectedClients;
		server->_Log("Client connected from %s",
			inet_ntoa(clientAddr.sin_addr));

		// Handle connection (in same thread for simplicity)
		server->_HandleConnection(clientSocket);

		--server->fConnectedClients;
		close(clientSocket);
	}

	return 0;
}


void
MCPServer::_HandleConnection(int socket)
{
	char buffer[8192];
	ssize_t bytesRead = recv(socket, buffer, sizeof(buffer) - 1, 0);

	if (bytesRead <= 0)
		return;

	buffer[bytesRead] = '\0';

	// Parse HTTP request
	BString request(buffer);

	// Extract method and path
	BString method, path;
	int32 spacePos = request.FindFirst(' ');
	if (spacePos > 0) {
		request.CopyInto(method, 0, spacePos);
		int32 secondSpace = request.FindFirst(' ', spacePos + 1);
		if (secondSpace > spacePos)
			request.CopyInto(path, spacePos + 1, secondSpace - spacePos - 1);
	}

	_Log("Request: %s %s", method.String(), path.String());

	// Handle CORS preflight
	if (method == "OPTIONS") {
		BString response;
		response << "HTTP/1.1 200 OK\r\n";
		response << "Access-Control-Allow-Origin: *\r\n";
		response << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
		response << "Access-Control-Allow-Headers: Content-Type\r\n";
		response << "Content-Length: 0\r\n";
		response << "\r\n";
		send(socket, response.String(), response.Length(), 0);
		return;
	}

	// MCP endpoints
	if (path == "/mcp" && method == "POST") {
		// Extract JSON body
		BString body;
		int32 bodyStart = request.FindFirst("\r\n\r\n");
		if (bodyStart > 0)
			request.CopyInto(body, bodyStart + 4, request.Length() - bodyStart - 4);

		++fTotalRequests;

		BString result = _HandleMCPRequest(body);
		_SendHTTPResponse(socket, 200, "OK", result);
	}
	else if (path == "/health" && method == "GET") {
		BString result = "{\"status\":\"ok\",\"server\":\"BubiCam MCP\",\"version\":\"1.0\"}";
		_SendHTTPResponse(socket, 200, "OK", result);
	}
	else if (path == "/" && method == "GET") {
		BString html;
		html << "<!DOCTYPE html><html><head><title>BubiCam MCP Server</title></head>";
		html << "<body><h1>BubiCam MCP Server</h1>";
		html << "<p>MCP endpoint: POST /mcp</p>";
		html << "<p>Health check: GET /health</p>";
		html << "<h2>Available Tools:</h2><ul>";
		for (int32 i = 0; i < fTools.CountItems(); i++) {
			MCPTool* tool = fTools.ItemAt(i);
			html << "<li><b>" << tool->name << "</b>: " << tool->description << "</li>";
		}
		html << "</ul></body></html>";
		_SendHTTPResponse(socket, 200, "OK", html, "text/html");
	}
	else {
		_SendHTTPResponse(socket, 404, "Not Found",
			"{\"error\":\"Not found\"}");
	}
}


void
MCPServer::_SendHTTPResponse(int socket, int statusCode, const char* statusText,
	const BString& body, const char* contentType)
{
	BString response;
	response << "HTTP/1.1 " << statusCode << " " << statusText << "\r\n";
	response << "Content-Type: " << contentType << "\r\n";
	response << "Content-Length: " << body.Length() << "\r\n";
	response << "Access-Control-Allow-Origin: *\r\n";
	response << "Connection: close\r\n";
	response << "\r\n";
	response << body;

	send(socket, response.String(), response.Length(), 0);
}


BString
MCPServer::_HandleMCPRequest(const BString& jsonRequest)
{
	// Extract method and id from JSON
	BString method = _ExtractJsonString(jsonRequest, "method");
	BString id = _ExtractJsonString(jsonRequest, "id");

	_Log("MCP method: %s, id: %s", method.String(), id.String());

	if (method == "initialize") {
		return _HandleInitialize(id);
	}
	else if (method == "tools/list") {
		return _HandleToolsList(id);
	}
	else if (method == "tools/call") {
		BString params = _ExtractJsonObject(jsonRequest, "params");
		BString toolName = _ExtractJsonString(params, "name");
		BString arguments = _ExtractJsonObject(params, "arguments");
		return _HandleToolCall(id, toolName, arguments);
	}
	else if (method == "notifications/initialized") {
		// Client notification, no response needed but we'll acknowledge
		return _JsonResult(id, "{}");
	}
	else {
		++fTotalErrors;
		return _JsonError(id, -32601, "Method not found");
	}
}


BString
MCPServer::_HandleInitialize(const BString& id)
{
	BString result;
	result << "{";
	result << "\"protocolVersion\":\"2024-11-05\",";
	result << "\"serverInfo\":{";
	result << "\"name\":\"BubiCam\",";
	result << "\"version\":\"1.0\"";
	result << "},";
	result << "\"capabilities\":{";
	result << "\"tools\":{}";
	result << "}";
	result << "}";

	return _JsonResult(id, result);
}


BString
MCPServer::_HandleToolsList(const BString& id)
{
	BString result;
	result << "{\"tools\":[";

	for (int32 i = 0; i < fTools.CountItems(); i++) {
		MCPTool* tool = fTools.ItemAt(i);
		if (i > 0)
			result << ",";
		result << "{";
		result << "\"name\":\"" << tool->name << "\",";
		result << "\"description\":\"" << tool->description << "\",";
		result << "\"inputSchema\":" << tool->inputSchema;
		result << "}";
	}

	result << "]}";

	return _JsonResult(id, result);
}


BString
MCPServer::_HandleToolCall(const BString& id, const BString& toolName,
	const BString& arguments)
{
	_Log("Tool call: %s", toolName.String());

	BString content;

	if (toolName == "capture_frame")
		content = _ToolCaptureFrame(arguments);
	else if (toolName == "get_driver_info")
		content = _ToolGetDriverInfo(arguments);
	else if (toolName == "set_resolution")
		content = _ToolSetResolution(arguments);
	else if (toolName == "get_status")
		content = _ToolGetStatus(arguments);
	else if (toolName == "get_supported_formats")
		content = _ToolGetSupportedFormats(arguments);
	else {
		++fTotalErrors;
		return _JsonError(id, -32602, "Unknown tool");
	}

	// Wrap in MCP tool result format
	BString result;
	result << "{\"content\":[{\"type\":\"text\",\"text\":";
	// Escape the content as JSON string
	result << "\"";
	for (int32 i = 0; i < content.Length(); i++) {
		char c = content[i];
		if (c == '"')
			result << "\\\"";
		else if (c == '\\')
			result << "\\\\";
		else if (c == '\n')
			result << "\\n";
		else if (c == '\r')
			result << "\\r";
		else if (c == '\t')
			result << "\\t";
		else
			result << c;
	}
	result << "\"";
	result << "}]}";

	return _JsonResult(id, result);
}


BString
MCPServer::_ToolCaptureFrame(const BString& arguments)
{
	WebcamDevice* device = _LockDevice();
	if (device == NULL)
		return "{\"error\":\"No webcam device available\"}";

	// Get current frame from video consumer
	BBitmap* bitmap = device->GetCurrentFrame();
	_UnlockDevice();
	if (bitmap == NULL)
		return "{\"error\":\"No frame available - is webcam streaming?\"}";

	// Convert to PNG
	BTranslatorRoster* roster = BTranslatorRoster::Default();
	BBitmapStream stream(bitmap);
	BMallocIO output;

	// Find PNG translator
	translator_id* translators;
	int32 numTranslators;
	roster->GetAllTranslators(&translators, &numTranslators);

	translator_id pngTranslator = 0;
	for (int32 i = 0; i < numTranslators; i++) {
		const translation_format* formats;
		int32 numFormats;
		roster->GetOutputFormats(translators[i], &formats, &numFormats);
		for (int32 j = 0; j < numFormats; j++) {
			if (formats[j].type == 'PNG ') {
				pngTranslator = translators[i];
				break;
			}
		}
		if (pngTranslator != 0)
			break;
	}
	delete[] translators;

	if (pngTranslator == 0) {
		stream.DetachBitmap(&bitmap);
		return "{\"error\":\"PNG translator not found\"}";
	}

	status_t status = roster->Translate(&stream, NULL, NULL, &output, 'PNG ');
	stream.DetachBitmap(&bitmap);  // Don't let stream delete the bitmap

	if (status != B_OK)
		return "{\"error\":\"Failed to encode PNG\"}";

	// Base64 encode
	BString base64 = Base64Encode((const uint8*)output.Buffer(), output.BufferLength());

	BString result;
	result << "{\"format\":\"png\",\"width\":" << bitmap->Bounds().IntegerWidth() + 1;
	result << ",\"height\":" << bitmap->Bounds().IntegerHeight() + 1;
	result << ",\"data\":\"" << base64 << "\"}";

	return result;
}


BString
MCPServer::_ToolGetDriverInfo(const BString& arguments)
{
	WebcamDevice* device = _LockDevice();
	if (device == NULL)
		return "{\"error\":\"No webcam device available\"}";

	BString result;
	result << "{";
	result << "\"name\":\"" << device->Name() << "\",";
	result << "\"driver\":\"" << device->DriverName() << "\",";
	result << "\"version\":\"" << device->DriverVersion() << "\"";
	result << "}";

	_UnlockDevice();
	return result;
}


BString
MCPServer::_ToolSetResolution(const BString& arguments)
{
	// Parse width and height from arguments
	BString widthStr = _ExtractJsonString(arguments, "width");
	BString heightStr = _ExtractJsonString(arguments, "height");

	int width = atoi(widthStr.String());
	int height = atoi(heightStr.String());

	if (width <= 0 || height <= 0)
		return "{\"error\":\"Invalid resolution\"}";

	WebcamDevice* device = _LockDevice();
	if (device == NULL)
		return "{\"error\":\"No webcam device available\"}";

	// Find matching format
	const BObjectList<VideoFormat>& formats = device->SupportedFormats();
	bool found = false;

	for (int32 i = 0; i < formats.CountItems(); i++) {
		VideoFormat* fmt = formats.ItemAt(i);
		if (fmt->width == width && fmt->height == height) {
			found = true;
			device->SetRequestedFormat(*fmt);
			break;
		}
	}
	_UnlockDevice();

	if (!found) {
		BString error;
		error << "{\"error\":\"Resolution not supported\",\"requested\":{\"width\":";
		error << width << ",\"height\":" << height << "}}";
		return error;
	}

	// Notify main window to restart preview
	BMessage msg('rsrt');  // restart preview
	fTarget.SendMessage(&msg);

	BString result;
	result << "{\"success\":true,\"resolution\":{\"width\":" << width;
	result << ",\"height\":" << height << "}}";

	return result;
}


BString
MCPServer::_ToolGetStatus(const BString& arguments)
{
	WebcamDevice* device = _LockDevice();
	if (device == NULL)
		return "{\"error\":\"No webcam device available\"}";

	VideoFormat currentFormat = device->CurrentFormat();
	uint32 framesReceived = device->FramesCaptured();
	uint32 framesDropped = device->FramesDropped();
	float actualFPS = device->CurrentFPS();
	bool isCapturing = device->IsCapturing();
	_UnlockDevice();

	BString result;
	result << "{";
	result << "\"streaming\":" << (isCapturing ? "true" : "false") << ",";
	result << "\"resolution\":{";
	result << "\"width\":" << currentFormat.width << ",";
	result << "\"height\":" << currentFormat.height;
	result << "},";
	result << "\"declared_fps\":" << currentFormat.frameRate << ",";
	result << "\"actual_fps\":" << actualFPS << ",";
	result << "\"frames_received\":" << framesReceived << ",";
	result << "\"frames_dropped\":" << framesDropped << ",";
	result << "\"frames_flowing\":" << (actualFPS > 0.5 ? "true" : "false") << ",";
	result << "\"colorSpace\":\"" << currentFormat.colorSpace << "\"";
	result << "}";

	return result;
}


BString
MCPServer::_ToolGetSupportedFormats(const BString& arguments)
{
	WebcamDevice* device = _LockDevice();
	if (device == NULL)
		return "{\"error\":\"No webcam device available\"}";

	const BObjectList<VideoFormat>& formats = device->SupportedFormats();

	BString result;
	result << "{\"formats\":[";

	for (int32 i = 0; i < formats.CountItems(); i++) {
		VideoFormat* fmt = formats.ItemAt(i);
		if (i > 0)
			result << ",";
		result << "{";
		result << "\"width\":" << fmt->width << ",";
		result << "\"height\":" << fmt->height << ",";
		result << "\"fps\":" << fmt->frameRate << ",";
		result << "\"colorSpace\":\"" << fmt->colorSpace << "\"";
		result << "}";
	}

	_UnlockDevice();
	result << "]}";

	return result;
}


BString
MCPServer::_JsonError(const BString& id, int code, const BString& message)
{
	BString result;
	result << "{\"jsonrpc\":\"2.0\",";
	if (id.Length() > 0)
		result << "\"id\":\"" << id << "\",";
	else
		result << "\"id\":null,";
	result << "\"error\":{\"code\":" << code << ",\"message\":\"" << message << "\"}}";
	return result;
}


BString
MCPServer::_JsonResult(const BString& id, const BString& resultContent)
{
	BString result;
	result << "{\"jsonrpc\":\"2.0\",";
	if (id.Length() > 0)
		result << "\"id\":\"" << id << "\",";
	else
		result << "\"id\":null,";
	result << "\"result\":" << resultContent << "}";
	return result;
}


BString
MCPServer::_ExtractJsonString(const BString& json, const char* key)
{
	BString searchKey;
	searchKey << "\"" << key << "\"";

	int32 keyPos = json.FindFirst(searchKey);
	if (keyPos < 0)
		return "";

	// Find colon after key
	int32 colonPos = json.FindFirst(':', keyPos + searchKey.Length());
	if (colonPos < 0)
		return "";

	// Skip whitespace
	int32 valueStart = colonPos + 1;
	while (valueStart < json.Length() &&
		   (json[valueStart] == ' ' || json[valueStart] == '\t' ||
		    json[valueStart] == '\n' || json[valueStart] == '\r'))
		valueStart++;

	if (valueStart >= json.Length())
		return "";

	// Check if value is a string (starts with quote)
	if (json[valueStart] == '"') {
		int32 valueEnd = valueStart + 1;
		while (valueEnd < json.Length()) {
			if (json[valueEnd] == '"' && json[valueEnd - 1] != '\\')
				break;
			valueEnd++;
		}
		BString value;
		json.CopyInto(value, valueStart + 1, valueEnd - valueStart - 1);
		return value;
	}
	// Check if it's a number
	else if (json[valueStart] >= '0' && json[valueStart] <= '9') {
		int32 valueEnd = valueStart;
		while (valueEnd < json.Length() &&
			   ((json[valueEnd] >= '0' && json[valueEnd] <= '9') || json[valueEnd] == '.'))
			valueEnd++;
		BString value;
		json.CopyInto(value, valueStart, valueEnd - valueStart);
		return value;
	}

	return "";
}


BString
MCPServer::_ExtractJsonObject(const BString& json, const char* key)
{
	BString searchKey;
	searchKey << "\"" << key << "\"";

	int32 keyPos = json.FindFirst(searchKey);
	if (keyPos < 0)
		return "{}";

	// Find colon after key
	int32 colonPos = json.FindFirst(':', keyPos + searchKey.Length());
	if (colonPos < 0)
		return "{}";

	// Skip whitespace
	int32 valueStart = colonPos + 1;
	while (valueStart < json.Length() &&
		   (json[valueStart] == ' ' || json[valueStart] == '\t' ||
		    json[valueStart] == '\n' || json[valueStart] == '\r'))
		valueStart++;

	if (valueStart >= json.Length() || json[valueStart] != '{')
		return "{}";

	// Find matching closing brace
	int32 braceCount = 1;
	int32 valueEnd = valueStart + 1;
	while (valueEnd < json.Length() && braceCount > 0) {
		if (json[valueEnd] == '{')
			braceCount++;
		else if (json[valueEnd] == '}')
			braceCount--;
		valueEnd++;
	}

	BString value;
	json.CopyInto(value, valueStart, valueEnd - valueStart);
	return value;
}


void
MCPServer::_Log(const char* format, ...)
{
	char buffer[512];
	va_list args;
	va_start(args, format);
	vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);

	fprintf(stderr, "[MCP] %s\n", buffer);

	// Send to GUI
	BMessage msg(MCP_SERVER_LOG);
	msg.AddString("message", buffer);
	fTarget.SendMessage(&msg);
}
