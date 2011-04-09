HeaderName = "PClipboard.h"
CodeFileName = "PClipboard.cpp"

ParentHeaderName = [["PHandler.h"]]

Includes = { "<Application.h>", "<Clipboard.h>", "<Window.h>", }

PObject = {}

PObject.name = "PClipboard"
PObject.description = "A representation of the clipboard"

-- This is for graphical controls which inherit PView somehow. It's
-- to make sure that the backend is handled as a BView derivative
PObject.usesView = false
PObject.getBackend = true
PObject.friendlyName = "Clipboard"
PObject.parentClass = "PHandler"
PObject.parentAccess = "public"

-- This is the code expected to go into the PObject::InitBackend method
PObject.initBackend = [[
	fBackend = new BClipboard("clipboard");
]]

PObject.properties =
{
	{ "Locked", "bool" , { "IsLocked", "void"}, { "Lock", "embedded" }, "", "0" },
	{ "Name", "string" , { "Name", "void"}, {  }, "", "0" },
	{ "LocalCount", "int" , { "LocalCount", "void"}, {  }, "", "0" },
	{ "SystemCount", "int" , { "SystemCount", "void"}, {  }, "", "0" },
}

PObject.embeddedProperties = {}

PObject.embeddedProperties["Locked"] = {}
PObject.embeddedProperties["Locked"].setCode = [[
		prop->GetValue(&boolval);
		if (*boolval.value)
			fBackend->Lock();
		else
			fBackend->Unlock();
]]


PBackend = {}
PBackend.name = "BClipboard"