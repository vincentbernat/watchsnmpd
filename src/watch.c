/*
 * Copyright (c) 2012 Vincent Bernat <bernat@luffy.cx>
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see
 * <http://www.gnu.org/licenses/>.
 */

#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>
#include <net-snmp/agent/net-snmp-agent-includes.h>

#include <sys/select.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include "log.h"

#define PROGNAME "watchsnmpd"

static void
usage() {
  printf("usage: " PROGNAME " [-d] [-x ADDRESS]\n"
	 "\n"
	 "\t-d: enable debug mode\n"
         "\t-i INTERVAL: interval to use between two pings\n"
         "\t-t TIMEOUT: timeout for detecting hanged master agent\n"
	 "\t-x ADDRESS: connect to master agent at ADDRESS\n");
  exit(0);
}

static int keep_running = 1;
static RETSIGTYPE
stop_server(int a) {
  (void)a;
  keep_running = 0;
}

void
loop(int timeout) {
  struct timeval snmp_timer_wait, *timer_wait;
  int snmpblock = 0;
  int fdsetsize;
  int num;
  fd_set readfd;
  struct timespec start, end;
  double diff;

  keep_running = 1;
  signal(SIGTERM, stop_server);
  signal(SIGINT,  stop_server);  
  while (keep_running) {
    fdsetsize = FD_SETSIZE;
    snmpblock = 1;
    FD_ZERO(&readfd);
    timer_wait = NULL;
    snmp_select_info(&fdsetsize, &readfd, &snmp_timer_wait, &snmpblock);
    if (snmpblock == 0) timer_wait = &snmp_timer_wait;

    num = select(FD_SETSIZE, &readfd, NULL, NULL, timer_wait);

    if (num < 0) {
      if (errno == EINTR) continue;
      log_warn("error in main loop");
      return;
    }
    clock_gettime(CLOCK_MONOTONIC, &start);
    if (num == 0) {
      snmp_timeout();
      run_alarms();
      netsnmp_check_outstanding_agent_requests();
    } else {
      snmp_read(&readfd);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    if (end.tv_sec < start.tv_sec ||
        (end.tv_sec == start.tv_sec && end.tv_nsec < start.tv_nsec)) {
      log_warnx("time is not increasing");
      continue;
    }
    diff = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec)/1000000000.;
    log_debug("n=%d, spent %fs", num, diff);

    if (diff > timeout) {
      log_warnx("while %s, we were blocked for %f seconds",
                (num==0)?"handling alarms":"reading packets",
                diff);
    }
  }  
}

int
main(int argc, char **argv) {
  int   ch;
  int   debug         = 0;
  int   interval      = 5;
  int   timeout       = 2;
  char *agentx_socket = NULL;

  /* Parse arguments */
  while ((ch = getopt(argc, argv, "di:t:x:")) != EOF)
    switch(ch) {
    case 'd':
      debug++;
      break;
    case 'i':
      interval = atoi(optarg);
      break;
    case 't':
      timeout = atoi(optarg);
      break;
    case 'x':
      agentx_socket = optarg;
      break;
    default:
      fprintf(stderr, "unknown option %c\n", ch);
      usage();
    }
  if (optind != argc)
    usage();

  log_init(debug, PROGNAME);

  /* Initialize Net-SNMP subagent */
  log_info("Initializing Net-SNMP subagent");
  netsnmp_enable_subagent();
  if (NULL != agentx_socket)
    netsnmp_ds_set_string(NETSNMP_DS_APPLICATION_ID,
			  NETSNMP_DS_AGENT_X_SOCKET, agentx_socket);

  /* Logging */
  snmp_disable_log();
  if (debug)
    snmp_enable_stderrlog();
  else
    snmp_enable_syslog_ident(PROGNAME, LOG_DAEMON);

  /* Setup agent */
  init_agent("watchsnmpd");
  netsnmp_ds_set_int(NETSNMP_DS_APPLICATION_ID,
                     NETSNMP_DS_AGENT_AGENTX_PING_INTERVAL, interval);
  init_snmp("watchsnmpd");

  /* Detach as a daemon */
  if (!debug && (daemon(0, 0) != 0))
    fatal("failed to detach daemon");

  loop(timeout);

  return 0;
}
