// Asset Graph Exporter

#include "AGEMcpState.h"
#include "HAL/PlatformTime.h"
#include "Misc/DateTime.h"

namespace AGE::McpStateDetail
{
	bool bServeMode = false;
	bool bShutdownRequested = false;
	int32 IdleMinutes = 0;
	FDateTime ServeStartUtc;
	double LastActivityTime = 0.0;
}

bool AGE::Mcp::IsServeMode()
{
	return McpStateDetail::bServeMode;
}

void AGE::Mcp::EnterServeMode(int32 IdleMinutes)
{
	McpStateDetail::bServeMode = true;
	McpStateDetail::IdleMinutes = FMath::Max(IdleMinutes, 0);
	McpStateDetail::ServeStartUtc = FDateTime::UtcNow();
	McpStateDetail::LastActivityTime = FPlatformTime::Seconds();
}

FDateTime AGE::Mcp::GetServeStartUtc()
{
	return McpStateDetail::ServeStartUtc;
}

int32 AGE::Mcp::GetIdleMinutes()
{
	return McpStateDetail::IdleMinutes;
}

void AGE::Mcp::NoteActivity()
{
	McpStateDetail::LastActivityTime = FPlatformTime::Seconds();
}

double AGE::Mcp::GetLastActivityTime()
{
	return McpStateDetail::LastActivityTime;
}

void AGE::Mcp::RequestShutdown()
{
	McpStateDetail::bShutdownRequested = true;
}

bool AGE::Mcp::IsShutdownRequested()
{
	return McpStateDetail::bShutdownRequested;
}
