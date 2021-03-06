#include "Globals.h"

#include <Application.h>
#include <Catalog.h>
#include <ctype.h>
#include <Directory.h>
#include <File.h>
#include <Locale.h>
#include <Path.h>
#include <Roster.h>
#include <stdio.h>

#include "BeIDEProject.h"
#include "DebugTools.h"
#include "DPath.h"
#include "FileFactory.h"
#include "Globals.h"
#include "Project.h"
#include "Settings.h"
#include "SourceTypeLib.h"
#include "StatCache.h"
#include <stdlib.h>
#include "TextFile.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Globals"

DPath gAppPath;
DPath gBackupPath;
DPath gProjectPath;
DPath gLastProjectPath;
DPath gSVNRepoPath;

bool gBuildMode = false;
bool gMakeMode = false;
bool gDontManageHeaders = true;
bool gSingleThreadedBuild = false;
bool gShowFolderOnOpen = false;
bool gAutoSyncModules = true;
bool gUseCCache = false;
bool gCCacheAvailable = false;
bool gUseFastDep = false;
bool gFastDepAvailable = false;
bool gHgAvailable = false;
bool gGitAvailable = false;
bool gSvnAvailable = false;
bool gLuaAvailable = false;
BString gDefaultEmail;

Project *gCurrentProject = NULL;
LockableList<Project> *gProjectList = NULL;
CodeLib gCodeLib;
scm_t gDefaultSCM = SCM_HG;
bool gUsePipeHack = false;

uint8 gCPUCount = 1;

StatCache gStatCache;
bool gUseStatCache = true;
platform_t gPlatform = PLATFORM_R5;


void
InitGlobals(void)
{
	app_info ai;
	be_app->GetAppInfo(&ai);
	BPath path(&ai.ref);
	gAppPath = path.Path();
	
	DPath settingsPath(B_USER_SETTINGS_DIRECTORY);
	settingsPath << "Paladin_settings";
	
	gSettings.Load(settingsPath.GetFullPath());
	
	gDontManageHeaders = gSettings.GetBool("dontmanageheaders",true);
	gSingleThreadedBuild = gSettings.GetBool("singlethreaded",false);
	gShowFolderOnOpen = gSettings.GetBool("showfolderonopen",false);
	gAutoSyncModules = gSettings.GetBool("autosyncmodules",true);
	gUseCCache = gSettings.GetBool("ccache",false);
	gUseFastDep = gSettings.GetBool("fastdep",false);
	
	gDefaultSCM = (scm_t)gSettings.GetInt32("defaultSCM", SCM_HG);
	
	system_info sysinfo;
	get_system_info(&sysinfo);
	gCPUCount = sysinfo.cpu_count;
	
	gPlatform = DetectPlatform();
	
	// This will make sure that we can still build if ccache is borked and the user
	// wants to use it.
	if ((gPlatform == PLATFORM_HAIKU || gPlatform == PLATFORM_HAIKU_GCC4 || gPlatform == PLATFORM_ZETA) &&
		system("ccache > /dev/null 2>&1") == 1)
	{
		gCCacheAvailable = true;
	}
		
	if (gPlatform == PLATFORM_HAIKU || gPlatform == PLATFORM_HAIKU_GCC4)
	{
		if (system("fastdep > /dev/null 2>&1") == 0)
			gFastDepAvailable = true;
		
		if (system("hg > /dev/null 2>&1") == 0)
			gHgAvailable = true;
		
		if (system("git > /dev/null 2>&1") == 1)
			gGitAvailable = true;
		
		gUsePipeHack = true;
	}
	
	if (system("svn > /dev/null 2>&1") == 1)
		gSvnAvailable = true;
	
	if (system("lua -v > /dev/null 2>&1") == 0)
		gLuaAvailable = true;
	
	gProjectPath.SetTo(gSettings.GetString("projectpath",PROJECT_PATH));
	gLastProjectPath.SetTo(gSettings.GetString("lastprojectpath",PROJECT_PATH));
	
	DPath defaultBackupPath(B_DESKTOP_DIRECTORY);
	gBackupPath.SetTo(gSettings.GetString("backuppath", defaultBackupPath.GetFullPath()));
	
	DPath defaultRepoPath(B_USER_DIRECTORY);
	defaultRepoPath << "Paladin SVN Repos";
	gSVNRepoPath.SetTo(gSettings.GetString("svnrepopath", defaultRepoPath.GetFullPath()));
	
	
	gCodeLib.ScanFolders();
}


void
EnsureTemplates(void)
{
	// Because creating a new project depends on the existence of the Templates folder,
	// make sure that we have some (very) basic templates to work with if the folder
	// has been deleted.
	DPath templatePath = gAppPath.GetFolder();
	templatePath << "Templates";
	
	bool missing = false;
	BDirectory tempDir;
	if (!BEntry(templatePath.GetFullPath()).Exists())
	{
		BDirectory appDir(gAppPath.GetFolder());
		appDir.CreateDirectory("Templates", &tempDir);
		missing = true;
	}
	else
	{
		tempDir.SetTo(templatePath.GetFullPath());
		if (tempDir.CountEntries() == 0)
			missing = true;
	}
	
	if (missing)
	{
		BDirectory dir;
		tempDir.CreateDirectory("Empty Application", &dir);
		tempDir.CreateDirectory("Kernel Driver", &dir);
		tempDir.CreateDirectory("Shared Library or Addon", &dir);
		tempDir.CreateDirectory("Static Library", &dir);
		
		DPath filePath;
		TextFile file;
		
		filePath = templatePath;
		filePath << "Empty Application/TEMPLATEINFO";
		file.SetTo(filePath.GetFullPath(), B_CREATE_FILE | B_READ_WRITE);
		file.WriteString("TYPE=Application\nLIB=B_BEOS_LIB_DIRECTORY/libsupc++.so\n");
		
		filePath = templatePath;
		filePath << "Kernel Driver/TEMPLATEINFO";
		file.SetTo(filePath.GetFullPath(), B_CREATE_FILE | B_READ_WRITE);
		file.WriteString("TYPE=Driver\n");
		
		filePath = templatePath;
		filePath << "Shared Library or Addon/TEMPLATEINFO";
		file.SetTo(filePath.GetFullPath(), B_CREATE_FILE | B_READ_WRITE);
		file.WriteString("TYPE=Shared\n");
		
		filePath = templatePath;
		filePath << "Static Library/TEMPLATEINFO";
		file.SetTo(filePath.GetFullPath(), B_CREATE_FILE | B_READ_WRITE);
		file.WriteString("TYPE=Static\n");
		
		file.Unset();
	}
}


entry_ref
MakeProjectFile(DPath folder, const char *name, const char *data, const char *type)
{
	entry_ref ref;
	
	DPath path(folder);
	path.Append(name);
	BEntry entry(path.GetFullPath());
	if (entry.Exists())
	{
		BString errstr = B_TRANSLATE("%filepath% already exists. Do you want to overwrite it?");
		errstr.ReplaceFirst("%filepath%", path.GetFullPath());
		int32 result = ShowAlert(errstr.String(),B_TRANSLATE("Overwrite"),B_TRANSLATE("Cancel"));
		if (result == 1)
			return ref;
	}
	
	BFile file(path.GetFullPath(),B_READ_WRITE | B_CREATE_FILE | B_ERASE_FILE);
	
	if (data && strlen(data) > 0)
		file.Write(data,strlen(data));
	
	BString fileType = (type && strlen(type) > 0) ? type : "text/x-source-code";
	file.WriteAttr("BEOS:TYPE",B_STRING_TYPE, 0, fileType.String(),
						fileType.Length() + 1);
	
	file.Unset();
	entry.GetRef(&ref);
	return ref;
}


BString
MakeHeaderGuard(const char *name)
{
	BString define(name);
	define.ReplaceSet(" .-","_");
	
	// Normally, we'd just put something like :
	// define[i] = toupper(define[i]);
	// in a loop, but the BString defines for Zeta are screwed up, so we're going to have to
	// work around them.
	char *buffer = define.LockBuffer(define.CountChars() + 1);
	for (int32 i = 0; i < define.CountChars(); i++)
		buffer[i] = toupper(buffer[i]);
	define.UnlockBuffer();
	
	BString guard;
	guard << "#ifndef " << define << "\n"
		<< "#define " << define << "\n"
			"\n"
			"\n"
			"\n"
			"#endif\n";
	return guard;
}


BString
MakeRDefTemplate(void)
{
	// Probably better done as a resource file. Oh well. *shrug*
	BString out = 	
	"/*--------------------------------------------------------------------\n"
	"	Change the value in quotes to match the signature passed\n"
	"	to the BApplication constructor by your program.\n"
	"--------------------------------------------------------------------*/\n"
	"resource app_signature \"application/x-vnd.me-MyAppSignature\";\n\n"
	"/*--------------------------------------------------------------------\n"
	"	Value for app flags can be B_SINGLE_LAUNCH, B_MULTIPLE_LAUNCH, or\n"
	"	B_EXCLUSIVE_LAUNCH.\n\n"
	"	Additionally, you may also add the B_BACKGROUND_APP or\n"
	"	B_ARGV_ONLY flags via a pipe symbol, such as the following:\n"
	"	B_SINGLE_LAUNCH | B_BACKGROUND_APP\n\n"
	"	B_SINGLE_LAUNCH is the normal OS behavior\n"
	"--------------------------------------------------------------------*/\n"
	"resource app_flags B_SINGLE_LAUNCH;\n\n"
	"/*--------------------------------------------------------------------\n"
	"	Set the version information about your app here.\n"
	"	The variety can be set to one of the following values\n"
	"	B_APPV_DEVELOPMENT,\n"
	"	B_APPV_ALPHA,\n"
	"	B_APPV_BETA,\n"
	"	B_APPV_GAMMA,\n"
	"	B_APPV_GOLDEN_MASTER,\n"
	"	B_APPV_FINAL\n"
	"--------------------------------------------------------------------*/\n"
	"resource app_version {\n"
	"	major  = 0,\n"
	"	middle = 0,\n"
	"	minor  = 1,\n\n"
	"	variety = B_APPV_DEVELOPMENT,\n"
	"	internal = 0,\n\n"
	"	short_info = \"A short app description\",\n"
	"	long_info = \"A longer app description\"\n"
	"};\n\n"
	"resource large_icon array {\n"
	"	$\"1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C\"\n"
	"	$\"1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C\"\n"
	"	$\"1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C\"\n"
	"	$\"1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C\"\n"
	"	$\"1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C\"\n"
	"	$\"1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C\"\n"
	"	$\"1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C\"\n"
	"	$\"1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C\"\n"
	"	$\"1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C\"\n"
	"	$\"1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C\"\n"
	"	$\"1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C\"\n"
	"	$\"1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C\"\n"
	"	$\"1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C\"\n"
	"	$\"1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C\"\n"
	"	$\"1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C\"\n"
	"	$\"1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C\"\n"
	"	$\"1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C\"\n"
	"	$\"1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C\"\n"
	"	$\"1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C\"\n"
	"	$\"1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C\"\n"
	"	$\"1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C\"\n"
	"	$\"1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C\"\n"
	"	$\"1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C\"\n"
	"	$\"1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C\"\n"
	"	$\"1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C\"\n"
	"	$\"1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C\"\n"
	"	$\"1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C\"\n"
	"	$\"1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C\"\n"
	"	$\"1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C\"\n"
	"	$\"1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C\"\n"
	"	$\"1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C\"\n"
	"	$\"1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C\"\n"
	"};\n\n"
	"resource mini_icon array {\n"
	"	$\"1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C\"\n"
	"	$\"1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C\"\n"
	"	$\"1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C\"\n"
	"	$\"1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C\"\n"
	"	$\"1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C\"\n"
	"	$\"1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C\"\n"
	"	$\"1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C\"\n"
	"	$\"1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C\"\n"
	"	$\"1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C\"\n"
	"	$\"1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C\"\n"
	"	$\"1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C\"\n"
	"	$\"1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C\"\n"
	"	$\"1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C\"\n"
	"	$\"1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C\"\n"
	"	$\"1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C\"\n"
	"	$\"1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C\"\n"
	"};\n";
	return out;
}


void
SetToolTip(BView *view, const char *text)
{
	if (!view || !text)
		return;
	
	#ifdef __HAIKU__
	view->SetToolTip(text);
	#endif
	
	#ifdef __ZETA__
	view->SetToolTipText(text);
	#endif
}


status_t
RunPipedCommand(const char *cmdstr, BString &out, bool redirectStdErr)
{
	if (!cmdstr)
		return B_BAD_DATA;
	
	BString command(cmdstr);
	out = "";
	
	if (gUsePipeHack)
	{
		BString tmpfilename("/tmp/Paladin.build.tmp.");
		tmpfilename << real_time_clock_usecs();
		
		command << " > " << tmpfilename;
		
		if (redirectStdErr)
			command << " 2>&1";
		system(command.String());
		
		BFile file(tmpfilename.String(), B_READ_ONLY);
		if (file.InitCheck() != B_OK)
		{
			STRACE(1,("Couldn't make temporary file for RunPipedCommand(\"%s\")\n",
						command.String()));
			return file.InitCheck();
		}
		
//		char buffer[1024];
//		while (file.Read(buffer, 1024) > 0)
//			out << buffer;

		off_t fileSize;
		file.GetSize(&fileSize);
		
		char buffer[1028];
		while (fileSize > 0)
		{
			size_t bytesRead = file.Read(buffer, fileSize > 1024 ? 1024 : fileSize);
			if (bytesRead <= 1024)
				buffer[bytesRead] = '\0';
			out << buffer;
			fileSize -= bytesRead;
		}
		
		file.Unset();
		BEntry(tmpfilename.String()).Remove();
	}
	else
	{
		if (redirectStdErr)
			command << " 2>&1";
		
		FILE *fd = popen(cmdstr,"r");
		if (!fd)
		{
			STRACE(1,("Bailed out on RunPipedCommand(\"%s\"): NULL pipe descriptor\n",
						command.String()));
			return B_BUSTED_PIPE;
		}
		
		char buffer[1024];
		BString errmsg;
		while (fgets(buffer,1024,fd)) {
			if (!ferror(fd)) {
				out += buffer;
			} else {
				STRACE(2,("pipe stream has failed"));
			}
		}
		int result = pclose(fd);
		if (0 == result) {
			STRACE(2,("pclose returned non zero result: %i",result));
		} else {
			STRACE(2,("pclose returned success (0)"));
		}
	}
	
	return B_OK;
}


status_t
BeIDE2Paladin(const char *path, BString &outpath)
{
	status_t returnVal = BEntry(path).InitCheck();
	if (returnVal != B_OK)
		return returnVal;
	
	BeIDEProject beide(path);
	if (beide.InitCheck() != B_OK)
		return beide.InitCheck();
	
	DPath dpath(path);
	Project proj(dpath.GetBaseName(), beide.TargetName());
	proj.SetPlatform(PLATFORM_R5);

	// NOTE: TARGET_* from Project.h & TARGET_* from BeIDEPRoject.h
	// map perfectly, so no explicit conversion required
	proj.SetTargetType(beide.TargetType());
	
	BString savepath(dpath.GetFolder());
	savepath << "/" << dpath.GetBaseName() << ".pld";
	proj.Save(savepath.String());
	
	for (int32 i = 0; i < beide.CountLocalIncludes(); i++)
	{
		BString include = beide.LocalIncludeAt(i);
		
		if (include.ICompare("{project}") == 0)
			continue;
		
		include.RemoveFirst("{project}/");
		proj.AddLocalInclude(include.String());
	}

	for (int32 i = 0; i < beide.CountSystemIncludes(); i++)
	{
		BString include = beide.SystemIncludeAt(i);
		
		if (include.ICompare("{project}") == 0)
			continue;
		
		include.RemoveFirst("{project}/");
		proj.AddSystemInclude(include.String());
	}
	
	SourceGroup *currentGroup = NULL;
	for (int32 i = 0; i < beide.CountFiles(); i++)
	{
		ProjectFile file = beide.FileAt(i);

		if (file.path.FindFirst("/_KERNEL_") > 0)
			continue;

		SourceFile *srcFile = gFileFactory.CreateSourceFileItem(file.path.String());
		
		if (!srcFile)
			continue;
		
		if (dynamic_cast<SourceFileLib*>(srcFile))
		{
			proj.AddLibrary(srcFile->GetPath().GetFileName());
			delete srcFile;
			continue;
		}
		
		if (!proj.HasGroup(file.group.String()))
			currentGroup = proj.AddGroup(file.group.String());

		BPath newPath;
		if (proj.LocateFile(srcFile->GetPath().GetFullPath(), newPath))
			srcFile->SetPath(newPath.Path());

		proj.AddFile(srcFile, currentGroup);
	}
	
	uint32 codeFlags = beide.CodeGenerationFlags();
	if (codeFlags & CODEGEN_DEBUGGING)
		proj.SetDebug(true);
	
	if (codeFlags & CODEGEN_OPTIMIZE_SIZE)
		proj.SetOpForSize(true);
	
	proj.SetOpLevel(beide.OptimizationMode());
	
	// Because Paladin doesn't currently support the seemingly 50,000 warning
	// types, we'll put them in the compiler options for the ones not commonly
	// used
	BString options;
	
	uint32 warnings = beide.Warnings();
	if (warnings & WARN_STRICT_ANSI)
		options << "-pedantic ";
	
	if (warnings & WARN_LOCAL_SHADOW)
		options << "-Wshadow ";
	
	if (warnings & WARN_INCOMPATIBLE_CAST)
		options << "-Wbad-function-cast ";
	
	if (warnings & WARN_CAST_QUALIFIERS)
		options << "-Wcast-qual ";
	
	if (warnings & WARN_CONFUSING_CAST)
		options << "-Wconversion ";
	
	if (warnings & WARN_CANT_INLINE)
		options << "-Winline ";
	
	if (warnings & WARN_EXTERN_TO_INLINE)
		options << "-Wextern-inline ";
	
	if (warnings & WARN_OVERLOADED_VIRTUALS)
		options << "-Woverloaded-virtual ";
	
	if (warnings & WARN_C_CASTS)
		options << "-Wold-style-cast ";
	
	if (warnings & WARN_EFFECTIVE_CPP)
		options << "-Weffc++ ";
	
	if (warnings & WARN_MISSING_PARENTHESES)
		options << "-Wparentheses ";
	
	if (warnings & WARN_INCONSISTENT_RETURN)
		options << "-Wreturn-type ";
	
	if (warnings & WARN_MISSING_ENUM_CASES)
		options << "-Wswitch ";
	
	if (warnings & WARN_UNUSED_VARS)
		options << "-Wunusued ";
	
	if (warnings & WARN_UNINIT_AUTO_VARS)
		options << "-Wuninitialized ";
	
	if (warnings & WARN_INIT_REORDERING)
		options << "-Wreorder ";
	
	if (warnings & WARN_NONVIRTUAL_DESTRUCTORS)
		options << "-Wnon-virtual-dtor ";
	
	if (warnings & WARN_UNRECOGNIZED_PRAGMAS)
		options << "-Wunknown-pragmas ";
	
	if (warnings & WARN_SIGNED_UNSIGNED_COMP)
		options << "-Wsign-compare ";
	
	if (warnings & WARN_CHAR_SUBSCRIPTS)
		options << "-Wchar-subscripts ";
	
	if (warnings & WARN_PRINTF_FORMATTING)
		options << "-Wformat ";
	
	if (warnings & WARN_TRIGRAPHS_USED)
		options << "-Wtrigraphs ";
	
	uint32 langopts = beide.LanguageOptions();
	if (langopts & LANGOPTS_ANSI_C_MODE)
		options << "-ansi ";
	
	if (langopts & LANGOPTS_SUPPORT_TRIGRAPHS)
		options << "-trigraphs ";
	
	if (langopts & LANGOPTS_SIGNED_CHAR)
		options << "-fsigned-char ";
	
	if (langopts & LANGOPTS_UNSIGNED_BITFIELDS)
		options << "-funsigned-bitfields ";
	
	if (langopts & LANGOPTS_CONST_CHAR_LITERALS)
		options << "-Wwrite-strings ";
	
	options << beide.ExtraCompilerOptions();
	proj.SetExtraCompilerOptions(options.String());
	proj.SetExtraLinkerOptions(beide.ExtraLinkerOptions());
	
	proj.Save();
	
	outpath = savepath;
	
	return B_OK;
}


bool
IsBeIDEProject(const entry_ref &ref)
{
	DPath dpath(ref);
	if (!dpath.GetExtension() || strcmp(dpath.GetExtension(), "proj") != 0)
		return false;
	
	BFile file(&ref, B_READ_ONLY);
	if (file.InitCheck() != B_OK)
		return false;
	
	char magic[5];
	if (file.Read(magic, 4) < 4)
		return false;
	magic[4] = '\0';
	return (strcmp(magic, "MIDE") == 0);
}


int32
ShowAlert(const char *message, const char *button1, const char *button2,
			const char *button3, alert_type type)
{
	int32 result;
	
	if (gBuildMode)
	{
		printf("%s\n", message);
		result = -1;
	}
	else
	{
		BString label1 = button1 ? button1 : B_TRANSLATE("OK");
		BAlert *alert = new BAlert(B_TRANSLATE_SYSTEM_NAME("Paladin"),
			message, label1.String(), button2, button3, B_WIDTH_AS_USUAL, type);
		result = alert->Go();
	}
	return result;
}


DPath
GetSystemPath(directory_which which)
{
	DPath out;
	
	BPath path;
	if (find_directory(which, &path) != B_OK)
		return out;
	
	out << path.Path();
	return out;
}


entry_ref
GetPartnerRef(entry_ref ref)
{
	DPath refpath(BPath(&ref).Path());
	BString ext(refpath.GetExtension());
	if (ext.CountChars() < 1)
		return entry_ref();
	
	BString pathbase = refpath.GetFullPath();
	pathbase.Truncate(pathbase.FindLast(".") + 1);
	
	const char *cpp_ext[] = { "cpp","c","cxx","cc", NULL };
	const char *hpp_ext[] = { "h","hpp","hxx","hh", NULL };
	
	bool isSource = false;
	bool isHeader = false;
	
	int i = 0;
	while (cpp_ext[i])
	{
		if (ext == cpp_ext[i])
		{
			isSource = true;
			break;
		}
		i++;
	}
	
	if (!isSource)
	{
		i = 0;
		while (hpp_ext[i])
		{
			if (ext == hpp_ext[i])
			{
				isHeader = true;
				break;
			}
			i++;
		}
	}
	else
	{
		i = 0;
		while (hpp_ext[i])
		{
			BString partpath = pathbase;
			partpath << hpp_ext[i];
			BEntry entry(partpath.String());
			if (entry.Exists())
			{
				entry_ref header_ref;
				entry.GetRef(&header_ref);
				return header_ref;
			}
			i++;
		}
	}
	
	if (isHeader)
	{
		i = 0;
		while (cpp_ext[i])
		{
			BString partpath = pathbase;
			partpath << cpp_ext[i];
			BEntry entry(partpath.String());
			if (entry.Exists())
			{
				entry_ref source_ref;
				entry.GetRef(&source_ref);
				return source_ref;
			}
			i++;
		}
	}
	
	return entry_ref();
}
