#include "HgSourceControl.h"

#include <Directory.h>
#include <Path.h>
#include <stdio.h>

#include "LaunchHelper.h"

HgSourceControl::HgSourceControl(void)
{
	SetShortName("hg");
	SetLongName("Mercurial");
	SetFlags(SCM_BRANCH | SCM_DISTRIBUTED);
}


HgSourceControl::HgSourceControl(const entry_ref &workingDir)
  :	SourceControl(workingDir)
{
	SetShortName("hg");
	SetLongName("Mercurial");
	SetFlags(SCM_BRANCH | SCM_DISTRIBUTED);
}


HgSourceControl::~HgSourceControl(void)
{
}


bool
HgSourceControl::NeedsInit(const char *topDir)
{
	if (!topDir)
		return false;
	
	// Checking for <project>/.hg won't work because of cases (ironically)
	// like Paladin's: the project might be under source control as part of
	// a larger tree. hg status returns 0 if under source control in a
	// tree, but 255 if not.
	ShellHelper sh;
	sh << "cd";
	sh.AddEscapedArg(GetWorkingDirectory());
	sh << "; hg status > /dev/null";
	
	int result = sh.Run();
	
	if (GetDebugMode())
	{
		printf("NeedsInit() command: %s\nResult: %s\n",
				sh.AsString().String(), (result != 0) ? "true" : "false");
	}
	
	return (sh.Run() != 0);
}


status_t
HgSourceControl::CreateRepository(const char *path)
{
	BString command;
	command << "hg ";
	
	if (GetVerboseMode())
		command << "-v ";
	
	command << "init '" << path << "'";
	
	BString out;
	RunCommand(command, out);
	SetWorkingDirectory(path);
	
	BPath hgpath(path);
	hgpath.Append(".hg");
	if (!BEntry(hgpath.Path()).Exists())
		return B_ERROR;
	
	SetWorkingDirectory(path);
	command = "";
	command << "cd '" << path << "'; hg ";
	
	if (GetVerboseMode())
		command << "-v ";
	
	command	<< " add";
	RunCommand(command, out);
	return B_OK;
}


bool
HgSourceControl::DetectRepository(const char *path)
{
	BEntry entry(path);
	if (!entry.Exists())
		return false;
	
	BPath repoPath(path);
	repoPath.Append(".hg");
	entry.SetTo(repoPath.Path());
	return entry.Exists();
}


status_t
HgSourceControl::CloneRepository(const char *url, const char *dest)
{
	if (!url || !dest)
		return B_BAD_DATA;
	
	BDirectory dir(dest);
	if (dir.InitCheck() != B_OK)
		create_directory(dest,0777);
	
	BString command;
	command << "hg ";
	
	if (GetVerboseMode())
		command << "-v ";
	
	command << "clone '" << url << "' '" << dest << "'";
	
	BString out;
	RunCommand(command, out);
	
	SetURL(url);
	SetWorkingDirectory(dest);
	return B_OK;
}


status_t
HgSourceControl::AddToRepository(const char *path)
{
	BString command;
	command << "cd '" << GetWorkingDirectory() << "'; hg -v ";
	command	<< "add -I '" << path << "'";
	
	BString out;
	RunCommand(command, out);
	return B_OK;
}


status_t
HgSourceControl::RemoveFromRepository(const char *path)
{
	BString command;
	command << "cd '" << GetWorkingDirectory() << "'; hg ";
	
	if (GetVerboseMode())
		command << "-v ";
	
	command	<< "remove -Af -I '" << path << "'";
	
	BString out;
	RunCommand(command, out);
	return B_OK;
}


status_t
HgSourceControl::Commit(const char *msg)
{
	BString command;
	command << "cd '" << GetWorkingDirectory() << "'; hg ";
	
	if (GetVerboseMode())
		command << "-v ";
	
	command	<< "commit -m '" << msg << "'";
	
	BString out;
	RunCommand(command, out);
	
	// Committing a revision doesn't typically print anything,
	// so we will print something for feedback.
	if (out.CountChars() < 1 && GetUpdateCallback())
		GetUpdateCallback()("Commit completed.\n");
		
	return B_OK;
}


// untested
status_t
HgSourceControl::Merge(const char *rev)
{
	BString command;
	command << "cd '" << GetWorkingDirectory() << "'; hg ";
	
	if (GetVerboseMode())
		command << "-v ";
	
	command	<< "merge ";
	
	if (rev)
		command	<< "'" << rev << "'";
	
	BString out;
	return (RunCommand(command, out) == 0) ? B_OK : B_ERROR;
}


// untested
status_t
HgSourceControl::Push(const char *url)
{
	if (url)
		SetURL(url);
	
	BString command;
	command << "cd '" << GetWorkingDirectory()
			<< "'; echo \"Pushing to repository...\"; hg ";
	
	if (GetVerboseMode())
		command << "-v ";
	
	command	<< "push '" << GetURL() << "'";
	
	BString out;
	return (RunCommand(command, out) == 0) ? B_OK : B_ERROR;
}


status_t
HgSourceControl::Pull(const char *url)
{
	if (url)
		SetURL(url);
	
	BString command;
	command << "cd '" << GetWorkingDirectory() << "'; hg ";
	
	if (GetVerboseMode())
		command << "-v ";
	
	command	<< "pull -u '" << GetURL() << "'";
	
	BString out;
	return (RunCommand(command, out) == 0) ? B_OK : B_ERROR;
}


// untested
void
HgSourceControl::Recover(void)
{
	BString command;
	command << "cd '" << GetWorkingDirectory() << "'; hg ";
	
	if (GetVerboseMode())
		command << "-v ";
	
	command	<< "recover";
	
	BString out;
	RunCommand(command, out);
}


status_t
HgSourceControl::Revert(const char *relPath)
{
	BString command;
	command << "cd '" << GetWorkingDirectory() << "'; hg -v ";
	
//	if (GetVerboseMode())
//		command << "-v ";
	
	command	<< "revert";
	
	if (relPath)
		command << " '" << relPath << "' --no-backup";
	else
		command << " --all";
	
	BString out;
	return (RunCommand(command, out) == 0) ? B_OK : B_ERROR;
}


// untested
status_t
HgSourceControl::Rename(const char *oldname, const char *newname)
{
	BString command;
	command << "cd '" << GetWorkingDirectory() << "'; hg ";
	
	if (GetVerboseMode())
		command << "-v ";
	
	command << "rename '" << oldname << "' '" << newname << "'";
	
	BString out;
	return (RunCommand(command, out) == 0) ? B_OK : B_ERROR;
}


status_t
HgSourceControl::Diff(const char *filename, const char *revision)
{
	BString command;
	command << "cd '" << GetWorkingDirectory() << "'; hg ";
	
	if (GetVerboseMode())
		command << "-v ";
	
	command << "diff '" << filename << "' ";
	
	if (revision)
		command << "-r " << revision;
	
	BString out;
	return (RunCommand(command, out) == 0) ? B_OK : B_ERROR;
}


status_t
HgSourceControl::GetHistory(BString &out, const char *file)
{
	BString command;
	command << "cd '" << GetWorkingDirectory() << "'; hg ";
	
	if (GetVerboseMode())
		command << "-v ";
	
	command	<<	"log ";
	
	if (file)
		command << "'" << file << "'";
	
	// Temporarily disable the update callback to eliminate redundancy
	// and prevent getting the info twice. ;-)
	SourceControlCallback temp = GetUpdateCallback();
	SetUpdateCallback(NULL);
	
	RunCommand(command, out);
	
	SetUpdateCallback(temp);
	
	return B_OK;
}


status_t
HgSourceControl::GetChangeStatus(BString &out)
{
	BString command;
	command << "cd '" << GetWorkingDirectory() << "'; hg ";
	
	if (GetVerboseMode())
		command << "-v ";
	
	command << "status";
	
	// Temporarily disable the update callback to eliminate redundancy
	// and prevent getting the info twice. ;-)
	SourceControlCallback temp = GetUpdateCallback();
	SetUpdateCallback(NULL);
	
	RunCommand(command, out);
	
	if (out.CountChars() < 1)
		out = "Repository is up-to-date.\n";
	
	SetUpdateCallback(temp);
	
	return B_OK;
}


status_t
HgSourceControl::GetCheckinHeader(BString &out)
{
	GetChangeStatus(out);
	
	out.Prepend("HG: \nAll lines starting with 'HG:' will be ignored.\n"
				"----------------------------------------------------\n");
	out.ReplaceAll("\n", "\nHG: ");
	
	return B_OK;
}

