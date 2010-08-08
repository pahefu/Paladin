#include "ProjectBuilder.h"

#include <Alert.h>
#include <Autolock.h>
#include <Directory.h>
#include <Entry.h>
#include <Path.h>
#include <Roster.h>
#include <stdlib.h>

#include "DebugTools.h"
#include "ErrorParser.h"
#include "Globals.h"
#include "LaunchHelper.h"
#include "Project.h"
#include "SourceFile.h"
#include "StatCache.h"
#include "TerminalWindow.h"

//#define BUILD_THREAD_TRACING

#ifdef BUILD_THREAD_TRACING
	#define BTRACE(x) printf x
#else
	#define BTRACE(x) /* */
#endif

ProjectBuilder::ProjectBuilder(void)
	:	fIsLinking(false),
		fIsBuilding(false),
		fManager(gCPUCount)
{
}


ProjectBuilder::ProjectBuilder(const BMessenger &target)
	:	fMsgr(target),
		fIsLinking(false),
		fIsBuilding(false),
		fManager(gCPUCount)
{
}


ProjectBuilder::~ProjectBuilder(void)
{
	if (IsBuilding())
		QuitBuild();
}


void
ProjectBuilder::BuildProject(Project *proj, int32 postbuild)
{
//	Build sequence:
//	Check to see if file needs built:
//		Is already marked? (saving changes to a source file marks it as changed)
//		Dependency file is modified?
//		Source mod time is newer than corresponding object mod time?
//		Corresponding object is missing?
//	Build file
//	Link all objects into executable
//	Add resources
//	Set appropriate executable attributes (type, icon, etc.)
	if (!proj)
		return;
	
	fProject = proj;
	fPostBuildAction = postbuild;

// This will work around a bug in Haiku's locking mechanism until such time that I
// can find and fix it
#if 0
	if (fProject->IsLocked())
	{
		BString outstr("Project locked at beginning of build. Holding thread is ");
		outstr << fProject->LockingThread();
		debugger(outstr.String());
	}
#else
	if ((gPlatform == PLATFORM_HAIKU || gPlatform == PLATFORM_HAIKU_GCC4) &&
		fProject->IsLocked() && fProject->LockingThread() == find_thread(NULL))
		fProject->Unlock();
#endif
	// Check for existence of object directory and create it when necessary
	BEntry entry(proj->GetObjectPath().GetFullPath());
	if (!entry.Exists())
		create_directory(proj->GetObjectPath().GetFullPath(),0777);
	
	STRACE(1,("Building Project %s\n",proj->GetName()));
	
	bool saveproj = false;
	
	// Always start the cache fresh on a new build
	gStatCache.MakeEmpty();
	
	// Check any files not already marked as needing built
	for (int32 i = 0; i < fProject->CountGroups(); i++)
	{
		SourceGroup *group = fProject->GroupAt(i);
		
		for (int32 j = 0; j < group->filelist.CountItems(); j++)
		{
			SourceFile *file = group->filelist.ItemAt(j);
			
			BMessage exmsg(M_EXAMINING_FILE);
			exmsg.AddPointer("file",file);
			fMsgr.SendMessage(&exmsg);
			
			BString dep = file->GetDependencies();
			if (proj->CheckNeedsBuild(file))
			{
				file->SetBuildFlag(BUILD_YES);
				BMessage drawmsg(M_FILE_NEEDS_BUILD);
				drawmsg.AddPointer("file",file);
				fMsgr.SendMessage(&drawmsg);
				fProject->MakeFileDirty(file);
				STRACE(1,("%s needs to be built\n",file->GetPath().GetFullPath()));
			}
			else
			{
				STRACE(1,("%s does not need to be built\n",file->GetPath().GetFullPath()));
			}
			if (!saveproj && dep.Compare(file->GetDependencies()) != 0)
				saveproj = true;
		}
	}
	
	if (saveproj)
		fProject->Save();
	
	fIsBuilding = true;
	
	int32 threadcount = 1;
	if (fProject->CountDirtyFiles() > 1 && !gSingleThreadedBuild)
	{
		// It's kind of silly spawning 4 threads on a quad core system to
		// build 2 files, so limit spawned threads to whichever is less
		threadcount = MIN(gCPUCount,fProject->CountDirtyFiles());
	}
	
	for (int32 i = 0; i < threadcount; i++)
		fManager.SpawnThread(BuildThread,this);
}


void
ProjectBuilder::QuitBuild(void)
{
	if (IsBuilding())
		fManager.QuitAllThreads();
}


bool
ProjectBuilder::IsBuilding(void)
{
	Lock();
	bool value = fIsBuilding;
	Unlock();
	return value;
}


void
ProjectBuilder::DoPostBuild(void)
{
	// It's really silly to try to run a library! ;-)
	if (fProject->TargetType() != TARGET_APP)
		return;
	
	BPath path(fProject->GetPath().GetFolder());
	path.Append(fProject->GetTargetName());
	
	LaunchHelper launcher;
	switch (fPostBuildAction)
	{
		case POSTBUILD_RUN:
		{
			DPath targetpath = fProject->GetPath().GetFolder();
			targetpath.Append(fProject->GetTargetName());
			launcher.SetRef(targetpath.GetFullPath());
			launcher.ParseToArgs(fProject->GetRunArgs());
			STRACE(1,("Run command: %s\n",launcher.AsString().String()));
			launcher.Launch();
			break;
		}
		case POSTBUILD_RUN_IN_TERMINAL:
		{
			BString command;
			DPath targetpath = fProject->GetPath().GetFolder();
			targetpath.Append(fProject->GetTargetName());
			command << "cd '" << targetpath.GetFolder() << "'; '"
				<< targetpath.GetFileName() << "' " << fProject->GetRunArgs()
				<< " 2>&1";
			
			STRACE(1,("Terminal Run command: %s\n",command.String()));
			
			TerminalWindow *termwin = new TerminalWindow(command.String());
			BString termtitle = "Terminal Output: ";
			termtitle << fProject->GetName();
			termwin->SetTitle(termtitle.String());
			termwin->Hide();
			termwin->Show();
			termwin->RunCommand();
			break;
		}
		case POSTBUILD_DEBUG:
		{
			// Can't check the Haiku version by using B_BEOS_VERSION, so
			// we have to depend on a small hack. R5 and Zeta don't have gdb,
			// so this shouldn't break unless someone changes this in Haiku
			if (BEntry("/boot/system/bin/gdb").Exists())
			{
				launcher.SetRef("/boot/system/apps/Terminal");
				launcher.AddArg("gdb");
			}
			else
			{
				if (BEntry("/boot/develop/tools/experimental/debugger/bdb").Exists())
					launcher.SetRef("/boot/develop/tools/experimental/debugger/bdb");
				else
				{
					// Stupid Zeta reorganization. Meh.
					if (BEntry("/boot/apps/Development/bdb/bdb").Exists())
						launcher.SetRef("/boot/apps/Development/bdb/bdb");
					else
					{
						BAlert *alert = new BAlert("Paladin","Paladin can't seem to "
													"find the debugger. Sorry.","OK");
						alert->Go();
						break;
					}
				}
			}
			
			BString targetPath(fProject->GetPath().GetFolder());
			targetPath << "/" << fProject->GetTargetName();
			launcher.AddArg(targetPath.String());
			launcher.ParseToArgs(fProject->GetRunArgs());
			STRACE(1,("Debugger command: %s\n",launcher.AsString().String()));
			launcher.Launch();
			break;
		}
		default:
		{
			break;
		}
	}
	fPostBuildAction = POSTBUILD_NOTHING;
}


void
ProjectBuilder::SendErrorMessage(ErrorList &list)
{
	BMessage errmsg;
	if (list.CountErrors() > 0)
		errmsg.what = M_BUILD_FAILURE;
	else if (list.msglist.CountItems() > 0)
		errmsg.what = M_BUILD_WARNINGS;
	else
		errmsg.what = M_BUILD_MESSAGES;
	list.Flatten(errmsg);
	fMsgr.SendMessage(&errmsg);
}


int32
ProjectBuilder::BuildThread(void *data)
{
	ProjectBuilder *parent = (ProjectBuilder *)data;
	Project *proj = parent->fProject;
	
	thread_id thisThread = find_thread(NULL);

	
	BMessage msg;
	BString errstr;
	bool link_needed = false;
	
	time_t lastMod = 0;
	
	proj->Lock();
	proj->SortDirtyList();
	
	int32 fileCount = proj->CountDirtyFiles();
	int32 filesBuilt = 0;
	
	SourceFile *file = proj->GetNextDirtyFile();
	if (file)
	{
		proj->MakeFileClean(file);
		lastMod = MAX(lastMod,file->GetModTime());
		file->UpdateModTime();
	}
	proj->Unlock();
	
	while (file)
	{
		link_needed = true;
		
		file->SetBuildFlag(BUILD_NO);
		
		filesBuilt++;
		
		msg.MakeEmpty();
		msg.what = M_BUILDING_FILE;
		msg.AddPointer("sourcefile",file);
		msg.AddInt32("count",filesBuilt);
		msg.AddInt32("total",fileCount);
		parent->fMsgr.SendMessage(&msg);
		
		BTRACE(("Thread %ld is building file %s\n",thisThread,file->GetPath().GetFileName()));
		
		BuildInfo *info = proj->GetBuildInfo();
		info->errorList.msglist.MakeEmpty();
		proj->PrecompileFile(file);
		
		if (info->errorList.msglist.CountItems() > 0)
		{
			parent->SendErrorMessage(info->errorList);
			
			if (info->errorList.CountErrors() > 0)
			{
				msg.MakeEmpty();
				msg.what = M_BUILDING_DONE;
				msg.AddPointer("sourcefile",file);
				parent->fMsgr.SendMessage(&msg);
				
				parent->Lock();
				parent->fIsBuilding = false;
				parent->Unlock();
				
				parent->fManager.RemoveThread(thisThread);
				parent->fManager.QuitAllThreads();
				
				BTRACE(("Thread %ld quit on errors after precompile\n",thisThread));
				
				return B_ERROR;
			}
			else
				info->errorList.msglist.MakeEmpty();
		}
		
		if (parent->fManager.ThreadCheckQuit())
		{
			BTRACE(("Thread %ld asked to quit after precompile\n",thisThread));
			
			parent->fManager.RemoveThread(thisThread);
			return B_OK;
		}
		
		proj->CompileFile(file);
		
		if (info->errorList.msglist.CountItems() > 0)
		{
			parent->SendErrorMessage(info->errorList);
			
			if (info->errorList.CountErrors() > 0)
			{
				msg.MakeEmpty();
				msg.what = M_BUILDING_DONE;
				msg.AddPointer("sourcefile",file);
				parent->fMsgr.SendMessage(&msg);
				
				parent->Lock();
				parent->fIsBuilding = false;
				parent->Unlock();
				
				parent->fManager.RemoveThread(thisThread);
				parent->fManager.QuitAllThreads();
				
				BTRACE(("Thread %ld quit after compile\n",thisThread));
				
				return B_ERROR;
			}
			else
				info->errorList.msglist.MakeEmpty();
		}
		
		msg.MakeEmpty();
		msg.what = M_BUILDING_DONE;
		msg.AddPointer("sourcefile",file);
		parent->fMsgr.SendMessage(&msg);
		
		if (parent->fManager.ThreadCheckQuit())
		{
			BTRACE(("Thread %ld asked to quit after compile\n",thisThread));
			
			parent->fManager.RemoveThread(thisThread);
			return B_OK;
		}
		
		proj->Lock();
		file = proj->GetNextDirtyFile();
		if (file)
		{
			proj->MakeFileClean(file);
			lastMod = MAX(lastMod,file->GetModTime());
			file->UpdateModTime();
		}
		proj->Unlock();
	}
	
	// Now that we've finished building the individual source files, we need to
	// link the whole thing together. No real special tricks are required -- just
	// lock the owning object, check to see if another thread is already doing the
	// job and, if so, return. If not, we'll set the flag and do the link and do
	// all the finishing work.
	parent->Lock();
	bool do_postprocess = true;
	if (parent->fIsLinking)
		do_postprocess = false;
	else
		parent->fIsLinking = true;
	parent->Unlock();
	
	if (do_postprocess)
	{
		while (parent->fManager.CountRunningThreads() > 1)
			snooze(10000);
		
		BTRACE(("Thread %ld is performing postcompile processing\n",thisThread));
		
		// Check to see if linking is needed
		BPath targetPath(proj->GetPath().GetFolder());
		targetPath.Append(proj->GetTargetName(),true);
		
		BEntry targetEntry(targetPath.Path());
		if (!targetEntry.Exists())
			link_needed = true;
		else
		{
			struct stat *s = gStatCache.StatFor(targetPath.Path());
			if (s || s->st_mtime < lastMod)
				link_needed = true;
		}
		
		BuildInfo *info = proj->GetBuildInfo();
		if (link_needed)
		{
			parent->fMsgr.SendMessage(M_LINKING_PROJECT);
			
			proj->Lock();
			proj->Link();
			
			if (info->errorList.msglist.CountItems() > 0)
			{
				parent->SendErrorMessage(info->errorList);
				
				if (info->errorList.CountErrors() > 0)
				{
					parent->Lock();
					parent->fIsLinking = false;
					parent->fIsBuilding = false;
					parent->Unlock();
					proj->Unlock();
					
					parent->fManager.RemoveThread(thisThread);
					parent->fManager.QuitAllThreads();
					
					BTRACE(("Thread %ld quit after linker errors\n",thisThread));
					
					return B_ERROR;
				}
				else
					info->errorList.msglist.MakeEmpty();
			}
			
			if (parent->fManager.ThreadCheckQuit())
			{
				BTRACE(("Thread %ld asked to quit after link\n",thisThread));
				parent->fManager.RemoveThread(thisThread);
				proj->Unlock();
				return B_OK;
			}
			
			proj->Unlock();
		}
		
		// Now that the linking is done, we should add any resource files
		parent->fMsgr.SendMessage(M_UPDATING_RESOURCES);
		
		proj->Lock();
		proj->UpdateResources();
		if (info->errorList.msglist.CountItems() > 0)
		{
			parent->SendErrorMessage(info->errorList);
			
			if (info->errorList.CountErrors() > 0)
			{
				parent->Lock();
				parent->fIsLinking = false;
				parent->fIsBuilding = false;
				parent->Unlock();
				proj->Unlock();
				
				parent->fManager.RemoveThread(thisThread);
				parent->fManager.QuitAllThreads();
				return B_ERROR;
			}
		}
		proj->UpdateAttributes();
		proj->Unlock();
		
		// Now that the linking is done, we should add any resource files
		parent->fMsgr.SendMessage(M_DOING_POSTBUILD);
		
		proj->Lock();
		int32 groupcount = proj->CountGroups();
		proj->Unlock();
		
		for (int32 j = 0; j < groupcount; j++)
		{
			// Locking isn't necessary here -- it reduces contention for the lock
			// and the build threads don't change the groups themselves
			SourceGroup *group = proj->GroupAt(j);
			int32 filecount = group->filelist.CountItems();
			
			for (int32 i = 0; i < filecount; i++)
			{
				proj->Lock();
				file = group->filelist.ItemAt(i);
				proj->PostBuild(file);
				proj->Unlock();
				
				if (info->errorList.msglist.CountItems() > 0)
				{
					parent->SendErrorMessage(info->errorList);
					info->errorList.msglist.MakeEmpty();
				}
			}
		}
		
		parent->Lock();
		parent->fIsLinking = false;
		parent->fIsBuilding = false;
		parent->Unlock();
		parent->fMsgr.SendMessage(M_BUILD_SUCCESS);
		
		parent->DoPostBuild();
	}
	parent->fManager.RemoveThread(thisThread);
	return B_OK;
}


ThreadManager::ThreadManager(uint8 max)
	:	fMaxThreads(max),
		fThreadCount(0),
		fQuitFlag(false)
{
	if (max < 1)
		fMaxThreads = 1;
	
	fThreadArray = new thread_id[max];
	
	memset(fThreadArray, -1, sizeof(thread_id) * fMaxThreads);
}


ThreadManager::~ThreadManager(void)
{
	QuitAllThreads();
	delete [] fThreadArray;
}


thread_id
ThreadManager::SpawnThread(thread_func func, void *data)
{
	BAutolock lock(fLock);
	
	if (fThreadCount == fMaxThreads)
		return B_ERROR;
	
	thread_id t = spawn_thread(func,"build thread", B_NORMAL_PRIORITY, data);
	if (t >= 0)
	{
		int8 slot = FindFreeSlot();
		if (slot >= 0)
		{
			BTRACE(("Spawning build thread %ld\n",t));
			resume_thread(t);
			fThreadArray[slot] = t;
			fThreadCount++;
		}
		else
			kill_thread(t);
	}
	
	return t;	
}


void
ThreadManager::RemoveThread(thread_id tid)
{
	BAutolock lock(fLock);
	
	for (int32 i = 0; i < fMaxThreads; i++)
	{
		if (fThreadArray[i] == tid)
		{
			fThreadArray[i] = -1;
			fThreadCount--;
			return;
		}
	}
}


uint8
ThreadManager::CountRunningThreads(void)
{
	BAutolock lock(fLock);
	uint8 count = fThreadCount;
	
	return count;
}


void
ThreadManager::QuitAllThreads(void)
{
	fLock.Lock();
	fQuitFlag = true;
	fLock.Unlock();
	
	while (true)
	{
		bool done = false;
		
		fLock.Lock();
		if (fThreadCount <= 0)
			done = true;
		fLock.Unlock();
		
		if (done)
			break;
		
		snooze(10000);
	}
	fQuitFlag = false;
}


void
ThreadManager::KillAllThreads(bigtime_t quit_timeout)
{
	BAutolock lock(fLock);
	fQuitFlag = true;
	
	if (quit_timeout > 0)
		snooze(quit_timeout);
	
	for (int32 i = 0; i < fMaxThreads; i++)
	{
		thread_id tid = fThreadArray[i];
		if (tid >= 0)
		{
			kill_thread(tid);
			fThreadArray[i] = -1;
		}
	}
	fThreadCount = 0;
}


bool
ThreadManager::ThreadCheckQuit(void)
{
	BAutolock lock(fLock);
	bool value = fQuitFlag;
	return value;
}


int8
ThreadManager::FindFreeSlot(void)
{
	for (int8 i = 0; i < fMaxThreads; i++)
	{
		thread_id tid = fThreadArray[i];
		if (tid < 0)
			return i;
	}
	return -1;
}
