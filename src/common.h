//
// common.h
// by Dylan Kress
//

#pragma once

// Prevent windows.h from including winsock.h (which conflicts with winsock2.h in msquic)
#define WIN32_LEAN_AND_MEAN
#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS 1
#endif

// MsQuic must come before any Windows includes
#include <msquic.h>

// Standard C includes
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>

// Windows-specific includes for high-resolution timestamps
#ifdef _WIN32
#include <windows.h>
#endif

// BOOLEAN type - Windows already defines this, so just use it
#ifndef BOOLEAN
typedef unsigned char BOOLEAN;
#endif
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef UNREFERENCED_PARAMETER
#define UNREFERENCED_PARAMETER(P) (void)(P)
#endif

// Timestamp logging helper
static inline void PrintTimestamp(void)
{
#ifdef _WIN32
	SYSTEMTIME st;
	GetLocalTime(&st);
	printf("[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
#else
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	struct tm *tm_info = localtime(&ts.tv_sec);
	printf("[%02d:%02d:%02d.%03ld] ", 
		tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec, ts.tv_nsec / 1000000);
#endif
}

// Logging macros with timestamps
#define LOG(...) do { PrintTimestamp(); printf(__VA_ARGS__); } while(0)
#define LOG_ERROR(...) do { PrintTimestamp(); fprintf(stderr, __VA_ARGS__); } while(0)
