#include "platform/file/AppLocation.hpp"

#include <Files.h>
#if !defined(LOKA_TOOLBOX_CLASSIC_6)
#include <Processes.h>
#endif

#include <cstring>

#include "platform/ToolboxHfsName.hpp"

namespace loka
{
  namespace platform
  {
    namespace file
    {
      bool ResolveApplicationItem(const loka::file::File &item, FileHandle &out)
      {
        out = FileHandle();
        if (!ApplicationRelativeIsOpenable(item))
        {
          return false;
        }

#if defined(LOKA_TOOLBOX_CLASSIC_6)
        return false;
#else
        Str63 name;
        if (!loka::toolbox::CopyApplicationRelativeToHfsPath(item.relativePath(), name))
        {
          return false;
        }

        ProcessSerialNumber process;
        if (GetCurrentProcess(&process) != noErr)
        {
          return false;
        }

        FSSpec applicationSpec;
        Str31 processName;
        ProcessInfoRec processInfo;
        std::memset(&processInfo, 0, sizeof(processInfo));
        processInfo.processInfoLength = sizeof(processInfo);
        processInfo.processName = processName;
        processInfo.processAppSpec = &applicationSpec;
        if (GetProcessInformation(&process, &processInfo) != noErr)
        {
          return false;
        }

        FSSpec resolvedSpec;
        const OSErr makeResult = FSMakeFSSpec(applicationSpec.vRefNum, applicationSpec.parID, name, &resolvedSpec);
        if (makeResult != noErr && makeResult != fnfErr)
        {
          return false;
        }

        FileHandle resolved;
        resolved.displayPath = loka::core::String::Literal("Application:") + item.relativePath();
        resolved.kind = item.kind();
        resolved.hasSpec = true;
        resolved.spec = resolvedSpec;
        out = resolved;
        return true;
#endif
      }

      bool ResolveApplicationSidecar(const loka::file::File &item, FileHandle &out)
      {
        return ResolveApplicationItem(item, out);
      }
    } // namespace file
  } // namespace platform
} // namespace loka
