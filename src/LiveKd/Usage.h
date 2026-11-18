// LiveKd usage help, transcribed byte-for-byte from sub_14000BBA0.
#pragma once

#include "LiveKdCommon.h"

namespace livekd
{
    // Usage, transcribed byte-for-byte from sub_14000BBA0.
    inline void PrintUsage()
    {
        static const char* const lines[] = {
            "usage:\n",
            "  livekd [-w|-k debugger-path|-o dumpfile] [-m[flags]] [-mp process|pid] [-vsym] [-b] [debugger "
            "options]\n",
            "  livekd [-w|-k debugger-path|-o dumpfile] -ml [-hvd] [-b] [debugger options]\n",
            "  livekd -hvl\n",
            "  livekd [-w|-k debugger-path|-o dumpfile] -hv guid|name [-p] [-vsym] [-b] [debugger options]\n",
            "  livekd [-w|-k debugger-path] -hv guid|name -hvkl [-vsym] [-b] [debugger options]\n",
            "   -hv     Specifies the name or GUID of the Hyper-V VM to debug.\n",
            "   -hvd    Includes hypervisor pages (Windows 8.1 and above only).\n",
            "   -hvl    Lists the names and GUIDs of running Hyper-V VMs.\n",
            "   -k      Specifies complete path and filename of debugger image to execute\n",
            "   -m      Creates a mirror dump, which is a consistent view of kernel memory\n",
            "           Only kernel mode memory will be available, and this option may need\n",
            "           significant amounts of available physical memory.  A flags mask that\n",
            "           specifies which regions to include may optionally be provided (drawn\n",
            "           from the following table, default 0x18F8):\n",
            "\n",
            "           0001 - process private,  0002 - mapped file,   0004 - shared section\n",
            "           0008 - page table pages, 0010 - paged pool,    0020 - non-paged pool\n",
            "           0040 - system PTEs,      0080 - session pages, 0100 - metadata files\n",
            "           0200 - AWE user pages,   0400 - driver pages,  0800 - kernel stacks\n",
            "           1000 - WS metadata,      2000 - large pages\n",
            "\n",
            "           The default captures most kernel memory contents and is recommended.\n",
            "           This option may be used with -o to save faster, consistent dumps.\n",
            "           Mirror dumps require Windows Vista or Windows Server 2008 or above.\n",
            "\n",
            "           Sysinternals RamMap provides a graphical summary of the distribution\n",
            "           of the available memory regions that can be selected for inclusion.\n",
            "   -ml     Generate live dump using native support (Windows 8.1 and above only).\n",
            "   -mp     Specifies a single process whose user mode memory contents should be\n",
            "           included in a mirror dump.  Only effective with the -m option.\n",
            "   -o      Saves a memory.dmp to disk instead of launching the debugger\n",
            "   -p      Pauses the target Hyper-V VM while LiveKd is active\n",
            "           (recommended for use with -o).\n",
            "   -vsym   Displays verbose debugging information about symbol load operations.\n",
            "   -w      Runs windbg instead of kd\n",
            "   -b      Batch mode, don't prompt to execute kd again after completion\n",
            "\n",
            "All other options are passed through to the debugger.\n",
            "Note: Use Ctrl-Break to terminate and restart the debugger if it hangs.\n\n",
            "By default LiveKd runs kd.exe.\n\n",
            nullptr};
        for (int i = 0; lines[i] != nullptr; ++i)
        {
            std::fputs(lines[i], stdout);
        }
    }
} // namespace livekd
