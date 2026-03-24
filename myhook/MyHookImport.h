#ifndef MY_HOOK_IMPORT_H
#define MY_HOOK_IMPORT_H

#ifdef _WIN32
    #ifdef MYHOOK_BUILD
        #define MYHOOK_API __declspec(dllexport)
    #else
        #define MYHOOK_API __declspec(dllimport)
    #endif
#else
    #define MYHOOK_API
#endif

#endif // MY_HOOK_IMPORT_H
