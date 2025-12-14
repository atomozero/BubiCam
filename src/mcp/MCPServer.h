/*
 * MCPServer.h - Model Context Protocol server for BubiCam
 *
 * Implements MCP over HTTP for integration with Claude Code
 * and other MCP-compatible clients.
 */

#ifndef MCP_SERVER_H
#define MCP_SERVER_H

#include <Handler.h>
#include <Looper.h>
#include <String.h>
#include <ObjectList.h>
#include <Messenger.h>
#include <NetworkAddress.h>
#include <atomic>

class BDataIO;
class WebcamDevice;

// Message constants for MCP server
enum {
	MCP_SERVER_START		= 'mcps',
	MCP_SERVER_STOP			= 'mcpt',
	MCP_SERVER_STATUS		= 'mcpu',
	MCP_SERVER_REQUEST		= 'mcpr',
	MCP_SERVER_CLIENT_CONNECTED	= 'mccc',
	MCP_SERVER_CLIENT_DISCONNECTED	= 'mccd',
	MCP_SERVER_LOG			= 'mcpl'
};

// MCP Tool definition
struct MCPTool {
	BString		name;
	BString		description;
	BString		inputSchema;	// JSON schema for parameters
};

// MCP Server statistics
struct MCPServerStats {
	bool		running;
	uint16		port;
	int32		connectedClients;
	int32		totalRequests;
	int32		totalErrors;
};


class MCPServer : public BLooper {
public:
						MCPServer(BMessenger target);
	virtual				~MCPServer();

	// Server control
	status_t			Start(uint16 port = 9847);
	status_t			Stop();
	bool				IsRunning() const { return fRunning; }
	uint16				Port() const { return fPort; }

	// Statistics
	MCPServerStats		GetStats() const;

	// Webcam device reference (for tools)
	void				SetWebcamDevice(WebcamDevice* device);

	// Tool management
	const BObjectList<MCPTool, true>&	Tools() const { return fTools; }

	// BLooper interface
	virtual void		MessageReceived(BMessage* message);

private:
	// HTTP handling
	static int32		_ListenerThread(void* data);
	void				_HandleConnection(int socket);
	void				_SendHTTPResponse(int socket, int statusCode,
							const char* statusText, const BString& body,
							const char* contentType = "application/json");
	void				_SendSSEEvent(int socket, const BString& event,
							const BString& data);

	// MCP protocol handling
	BString				_HandleMCPRequest(const BString& jsonRequest);
	BString				_HandleInitialize(const BString& id);
	BString				_HandleToolsList(const BString& id);
	BString				_HandleToolCall(const BString& id,
							const BString& toolName, const BString& arguments);

	// Tool implementations
	BString				_ToolCaptureFrame(const BString& arguments);
	BString				_ToolGetDriverInfo(const BString& arguments);
	BString				_ToolSetResolution(const BString& arguments);
	BString				_ToolGetStatus(const BString& arguments);
	BString				_ToolGetSupportedFormats(const BString& arguments);

	// JSON helpers
	BString				_JsonError(const BString& id, int code,
							const BString& message);
	BString				_JsonResult(const BString& id, const BString& result);
	BString				_ExtractJsonString(const BString& json,
							const char* key);
	BString				_ExtractJsonObject(const BString& json,
							const char* key);

	// Logging
	void				_Log(const char* format, ...);

	// Member variables
	BMessenger			fTarget;
	WebcamDevice*		fWebcamDevice;
	BObjectList<MCPTool, true>	fTools;

	std::atomic<bool>	fRunning;		// Thread-safe flag for server state
	uint16				fPort;
	thread_id			fListenerThread;
	int					fServerSocket;

	std::atomic<int32>	fConnectedClients;	// Thread-safe counters
	std::atomic<int32>	fTotalRequests;
	std::atomic<int32>	fTotalErrors;
};

#endif // MCP_SERVER_H
