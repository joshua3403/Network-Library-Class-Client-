#include "stdafx.h"
#include "CCrashDumpClass.h"
long CCrashDump::_lDumpCount = 0;

CCrashDump* pDump = CCrashDump::GetInstance();