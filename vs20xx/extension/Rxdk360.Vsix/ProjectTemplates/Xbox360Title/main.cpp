// $projectname$ - an Xbox 360 title built with the stock XDK on modern Visual Studio.
#include <xtl.h>

extern "C" int DbgPrint(const char *, ...);

void __cdecl main()
{
    DbgPrint("$projectname$: hello from the Xbox 360\n");
}
