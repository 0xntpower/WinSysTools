// TaskEnumerator.h -- walks the Windows Task Scheduler through the Task
// Scheduler 2.0 COM API, the original's default data source (LoadFrom = 1).
#pragma once
#include "TaskRecord.h"
#include <vector>
#include <string>

// Connects to the task service (local when computerName is empty, otherwise the
// named remote computer), walks every folder from the root "\\" recursively and
// returns one TaskRecord per IRegisteredTask, with all 37 display columns built.
// Mirrors the original: CoCreateInstance(CLSID_TaskScheduler, IID_ITaskService)
// -> Connect -> GetFolder("\\") walked with GetTasks(TASK_ENUM_HIDDEN) and
// GetFolders. COM must already be initialised on the calling thread. The WUA
// HRESULT is returned through hrOut (S_OK on success).
std::vector<TaskRecord> EnumerateTasks(const DisplayOptions& opt,
                                       const std::wstring& computerName,
                                       HRESULT* hrOut = nullptr);

enum class TaskOp { Enable, Disable, Run, Stop, GetXml };

// Perform an operation on a single task identified by its full scheduler path
// (for example "\\Microsoft\\Windows\\...\\Name"). For GetXml the task's
// definition XML is returned, otherwise an empty string. Succeeds through
// *ok when the operation completed (enable/disable/run/stop need the right
// privileges). COM must already be initialised on the calling thread.
std::wstring TaskOperation(TaskOp op, const std::wstring& taskPath,
                           const std::wstring& computerName, bool* ok = nullptr);
