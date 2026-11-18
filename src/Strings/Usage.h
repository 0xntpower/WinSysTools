// Strings output helpers: the message stream selector and the usage text
// (mirrors sub_140003DC0).
#pragma once

#include "StringsUtil.h"

#include <cstdio>

namespace strings
{
    [[nodiscard]] inline FILE* MessageStream()
    {
        return StdoutIsPipe() ? stdout : stderr;
    }

    inline int PrintUsage(const char* program)
    {
        FILE* out = MessageStream();
        std::fprintf(out, "usage: %s [-a] [-f offset] [-b bytes] [-n length] [-o] [-s] [-u] <file or directory>\n",
                     program);
        std::fprintf(out, "-a     Ascii-only search (Unicode and Ascii is default)\n");
        std::fprintf(out, "-b     Bytes of file to scan\n");
        std::fprintf(out, "-f     File offset at which to start scanning.\n");
        std::fprintf(out, "-o     Print offset in file string was located\n");
        std::fprintf(out, "-n     Minimum string length (default is 3)\n");
        std::fprintf(out, "-s     Recurse subdirectories\n");
        std::fprintf(out, "-u     Unicode-only search (Unicode and Ascii is default)\n");
        std::fprintf(out, "-nobanner\n");
        std::fprintf(out, "       Do not display the startup banner and copyright message.\n");
        std::fprintf(out, "\n");
        return -1;
    }
} // namespace strings
