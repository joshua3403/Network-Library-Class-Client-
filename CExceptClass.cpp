#include "stdafx.h"
#include "CExceptClass.h"

void CExceptClass::PrintInput(int size)
{
	time_t now = time(NULL);
	struct tm date;
	localtime_s(&date, &now);
	WCHAR errortext[256] = { 0 };
	WCHAR time[64] = { 0 };
	pFile = _wfopen(m_textTitle, L"ab");
	fwprintf_s(pFile, L"%s", m_szError);

	wcsftime(time, 64, L" _%Y%m%d_%H%M%S.txt\n", &date);

	fwprintf_s(pFile, L" %s", L"DATA : ");
	for (int i = 0; i < size; i++)
	{
		fprintf_s(pFile, "%d, ", m_szData[i]);
	}
	//fprintf_s(pFile, "%d, ", m_szData);

	fwprintf_s(pFile, L"%s", time);

	fclose(pFile);
}