//
// Based on example/sample_client.c from librelp
// (Apache-2.0 license, Copyright 2014 Mathias Nyman)
//
// Build with: gcc -O2 -lrelp -o relp-test relp-test.c && strip relp-test
//
// Usage (print usage info): ./relp-test
// Usage (example): ./relp-test 10.0.0.1 514 60 34 myhost myapp 'some message'
//   First two arguments are destination RELP host/port.
//   "60" is RELP sending timeout, in seconds.
//   "34" is "priority value" tag = facility * 8 + severity.
//     See RFC 3164 (or later ones) for details on this (34 = auth.crit).
//   After that follows local hostname, app name and message string.
//

#define __STDC_WANT_LIB_EXT2__ 1
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <error.h>
#include <errno.h>
#include <time.h>
#include "librelp.h"

#define TRY(f) if(f != RELP_RET_OK) error(37, 0, "ERROR - librelp call failed - %s", #f);

static relpEngine_t *pRelpEngine;

static void __attribute__((format(printf, 1, 2)))
dbgprintf(char *fmt, ...) {
	va_list ap;
	char pszWriteBuf[32*1024+1];
	va_start(ap, fmt);
	vsnprintf(pszWriteBuf, sizeof(pszWriteBuf), fmt, ap);
	va_end(ap);
	// printf("relp-debug: %s", pszWriteBuf);
}

int main(int argc, char *argv[]) {
	if (argc != 8) {
		printf( "Usage: %s relp-host relp-port relp-timeout"
			" msg-type msg-host msg-proc message\n", argv[0] );
		printf( "Will send RELP message with"
			" current date/time and specified parameters.\n" );
		return -1; }

	unsigned char *target = (unsigned char*)argv[1];
	unsigned char *port = (unsigned char*)argv[2];
	int protFamily = 2; /* IPv4=2, IPv6=10 */

	unsigned int timeout;
	unsigned int err = sscanf(argv[3], "%d", &timeout);
	if (!err) error(1, 0, "ERROR - timeout number conversion failed");

	char msg_time[200];
	time_t msg_time_t = time(NULL);
	struct tm *msg_time_tm = gmtime(&msg_time_t);
	if (!msg_time_tm) error(1, errno, "ERROR - gmtime conversion failed");
	if (!strftime(
				msg_time, sizeof(msg_time),
				"%Y-%m-%dT%H:%M:%S+00:00", msg_time_tm ))
			error(1, 0, "ERROR - strftime failed");

	char *msg;
	int msg_len = asprintf(
		&msg, "<%s>%s %s %s[-]: %s",
		argv[4], msg_time, argv[5], argv[6], argv[7] );

	relpClt_t *pRelpClt = NULL;

	TRY(relpEngineConstruct(&pRelpEngine));
	TRY(relpEngineSetDbgprint(pRelpEngine, dbgprintf));
	TRY(relpEngineSetEnableCmd( pRelpEngine,
		(unsigned char*)"syslog", eRelpCmdState_Required ));
	TRY(relpEngineCltConstruct(pRelpEngine, &pRelpClt));
	TRY(relpCltSetTimeout(pRelpClt, timeout));
	TRY(relpCltConnect(pRelpClt, protFamily, port, target));

	unsigned char *pMsg = (unsigned char*)msg;
	size_t lenMsg = strlen((char*) pMsg);
	TRY(relpCltSendSyslog(pRelpClt, pMsg, lenMsg));

	TRY(relpEngineCltDestruct(pRelpEngine, &pRelpClt));
	TRY(relpEngineDestruct(&pRelpEngine));

	free(msg);
	return 0;
}
