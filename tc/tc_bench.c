/*
 * tc.c		"tc" utility frontend.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Authors:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *
 * Fixes:
 *
 * Petri Mattila <petri@prihateam.fi> 990308: wrong memset's resulted in faults
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <syslog.h>/Users/chen/coding/iproute2/tc/tc.c
#include <fcntl.h>
#include <dlfcn.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <errno.h>

#include "SNAPSHOT.h"
#include "utils.h"
#include "tc_util.h"
#include "tc_common.h"

int show_stats = 0;
int show_details = 0;
int show_raw = 0;
int show_pretty = 0;

int resolve_hosts = 0;
int use_iec = 0;
int force = 0;
struct rtnl_handle rth;

static void *BODY = NULL;	/* cached handle dlopen(NULL) */
static struct qdisc_util * qdisc_list;
static struct filter_util * filter_list;

static int print_noqopt(struct qdisc_util *qu, FILE *f,
			struct rtattr *opt)
{
	if (opt && RTA_PAYLOAD(opt))
		fprintf(f, "[Unknown qdisc, optlen=%u] ",
			(unsigned) RTA_PAYLOAD(opt));
	return 0;
}

static int parse_noqopt(struct qdisc_util *qu, int argc, char **argv, struct nlmsghdr *n)
{
	if (argc) {
		fprintf(stderr, "Unknown qdisc \"%s\", hence option \"%s\" is unparsable\n", qu->id, *argv);
		return -1;
	}
	return 0;
}

static int print_nofopt(struct filter_util *qu, FILE *f, struct rtattr *opt, __u32 fhandle)
{
	if (opt && RTA_PAYLOAD(opt))
		fprintf(f, "fh %08x [Unknown filter, optlen=%u] ",
			fhandle, (unsigned) RTA_PAYLOAD(opt));
	else if (fhandle)
		fprintf(f, "fh %08x ", fhandle);
	return 0;
}

static int parse_nofopt(struct filter_util *qu, char *fhandle, int argc, char **argv, struct nlmsghdr *n)
{
	__u32 handle;

	if (argc) {
		fprintf(stderr, "Unknown filter \"%s\", hence option \"%s\" is unparsable\n", qu->id, *argv);
		return -1;
	}
	if (fhandle) {
		struct tcmsg *t = NLMSG_DATA(n);
		if (get_u32(&handle, fhandle, 16)) {
			fprintf(stderr, "Unparsable filter ID \"%s\"\n", fhandle);
			return -1;
		}
		t->tcm_handle = handle;
	}
	return 0;
}

struct qdisc_util *get_qdisc_kind(const char *str)
{
	void *dlh;
	char buf[256];
	struct qdisc_util *q;

	for (q = qdisc_list; q; q = q->next)
		if (strcmp(q->id, str) == 0)
			return q;

	snprintf(buf, sizeof(buf), "%s/q_%s.so", get_tc_lib(), str);
	dlh = dlopen(buf, RTLD_LAZY);
	if (!dlh) {
		/* look in current binary, only open once */
		dlh = BODY;
		if (dlh == NULL) {
			dlh = BODY = dlopen(NULL, RTLD_LAZY);
			if (dlh == NULL)
				goto noexist;
		}
	}

	snprintf(buf, sizeof(buf), "%s_qdisc_util", str);
	q = dlsym(dlh, buf);
	if (q == NULL)
		goto noexist;

reg:
	q->next = qdisc_list;
	qdisc_list = q;
	return q;

noexist:
	q = malloc(sizeof(*q));
	if (q) {

		memset(q, 0, sizeof(*q));
		q->id = strcpy(malloc(strlen(str)+1), str);
		q->parse_qopt = parse_noqopt;
		q->print_qopt = print_noqopt;
		goto reg;
	}
	return q;
}


struct filter_util *get_filter_kind(const char *str)
{
	void *dlh;
	char buf[256];
	struct filter_util *q;

	for (q = filter_list; q; q = q->next)
		if (strcmp(q->id, str) == 0)
			return q;

	snprintf(buf, sizeof(buf), "%s/f_%s.so", get_tc_lib(), str);
	dlh = dlopen(buf, RTLD_LAZY);
	if (dlh == NULL) {
		dlh = BODY;
		if (dlh == NULL) {
			dlh = BODY = dlopen(NULL, RTLD_LAZY);
			if (dlh == NULL)
				goto noexist;
		}
	}

	snprintf(buf, sizeof(buf), "%s_filter_util", str);
	q = dlsym(dlh, buf);
	if (q == NULL)
		goto noexist;

reg:
	q->next = filter_list;
	filter_list = q;
	return q;
noexist:
	q = malloc(sizeof(*q));
	if (q) {
		memset(q, 0, sizeof(*q));
		strncpy(q->id, str, 15);
		q->parse_fopt = parse_nofopt;
		q->print_fopt = print_nofopt;
		goto reg;
	}
	return q;
}

static void usage(void)
{
	fprintf(stderr, "Usage: tc [ OPTIONS ] OBJECT { COMMAND | help （CUC）}\n"
			"       tc [-force] -batch filename\n"
	                "where  OBJECT := { qdisc | class | filter | action | monitor }\n"
	                "       OPTIONS := { -s[tatistics] | -d[etails] | -r[aw] | -p[retty] | -b[atch] [filename] }\n");
}

static int do_cmd(int argc, char **argv)
{
	if (matches(*argv, "qdisc") == 0)
		return do_qdisc(argc-1, argv+1);

	if (matches(*argv, "class") == 0)
		return do_class(argc-1, argv+1);

	if (matches(*argv, "filter") == 0)
		return do_filter(argc-1, argv+1);

	if (matches(*argv, "actions") == 0)
		return do_action(argc-1, argv+1);

	if (matches(*argv, "monitor") == 0)
		return do_tcmonitor(argc-1, argv+1);

	if (matches(*argv, "help") == 0) {
		usage();
		return 0;
	}

	fprintf(stderr, "Object \"%s\" is unknown, try \"tc help\".\n",
		*argv);
	return -1;
}

static int batch(const char *name)
{
	char *line = NULL;
	size_t len = 0;
	int ret = 0;

	if (name && strcmp(name, "-") != 0) {
		if (freopen(name, "r", stdin) == NULL) {
			fprintf(stderr, "Cannot open file \"%s\" for reading: %s\n",
				name, strerror(errno));
			return -1;
		}
	}

	tc_core_init();

	if (rtnl_open(&rth, 0) < 0) {
		fprintf(stderr, "Cannot open rtnetlink\n");
		return -1;
	}

	cmdlineno = 0;
	while (getcmdline(&line, &len, stdin) != -1) {
		char *largv[100];
		int largc;

		largc = makeargs(line, largv, 100);
		if (largc == 0)
			continue;	/* blank line */

		if (do_cmd(largc, largv)) {
			fprintf(stderr, "Command failed %s:%d\n", name, cmdlineno);
			ret = 1;
			if (!force)
				break;
		}
	}
	if (line)
		free(line);

	rtnl_close(&rth);
	return ret;
}


int main(int argc, char **argv)
{
	int ret;
	int do_batching = 0;
	char *batchfile = NULL;

	while (argc > 1) {
		if (argv[1][0] != '-')
			break;
		if (matches(argv[1], "-stats") == 0 ||
			 matches(argv[1], "-statistics") == 0) {
			++show_stats;
		} else if (matches(argv[1], "-details") == 0) {
			++show_details;
		} else if (matches(argv[1], "-raw") == 0) {
			++show_raw;
		} else if (matches(argv[1], "-pretty") == 0) {
			++show_pretty;
		} else if (matches(argv[1], "-Version") == 0) {
			printf("tc utility, iproute2-ss%s\n", SNAPSHOT);
			return 0;
		} else if (matches(argv[1], "-iec") == 0) {
			++use_iec;
		} else if (matches(argv[1], "-help") == 0) {
			usage();
			return 0;
		} else if (matches(argv[1], "-force") == 0) {
			++force;
		} else 	if (matches(argv[1], "-batch") == 0) {
			do_batching = 1;
			if (argc > 2)
				batchfile = argv[2];
			argc--;	argv++;
		} else {
			fprintf(stderr, "Option \"%s\" is unknown, try \"tc -help\".\n", argv[1]);
			return -1;
		}
		argc--;	argv++;
	}

	if (do_batching)
		return batch(batchfile);

	if (argc <= 1) {
		usage();
		return 0;
	}

	tc_core_init();
	if (rtnl_open(&rth, 0) < 0) {
		fprintf(stderr, "Cannot open rtnetlink\n");
		exit(1);
	}

	// 执行 change 命令比 show 命令会增加 3-5us 延时左右
	argc=5; my_show_queue_argv(&argc, argv);  	// class show 命令初始化
	argc=12; my_change_queue_argv(&argc, argv); // class change 命令初始化

	printf("debug: default %d argvs: %s, %s, %s, %s\n", argc, argv[1], argv[2], argv[3], argv[4]);
	//argv[1]=
	//return;

	// 测速相关变量定义
	//int64_t count = 5; //查询次数
	int64_t count=1000000; //测速 循环数 average latency: 3506 ns (4us) / real	0m7.015s / CPU 100%
	unsigned int polling_interval=0; 			//采样间隔时间, 默认0，不休眠
	polling_interval =  1; count=100000; 	// 增加采样间隔 1微秒 ; 37-42us / real	0m7.447s / CPU 1-2%
	polling_interval = 10; count=100000; 	// 增加采样间隔10微秒 ; 47-50us / real	0m9.412s / CPU 1-2%
	// #ifdef HAS_CLOCK_GETTIME_MONOTONIC //linux 不用这个定义
	// 测速开始： 定义时间变量， 获取初始时间
	int64_t i, delta;
  struct timeval start, stop;
	if (gettimeofday(&start, NULL) == -1) {
      perror("gettimeofday");
      return 1;
  }

	// 测速循环
	for (i=0; i<count; i++){
		ret = do_cmd(argc-1, argv+1); 
		if (polling_interval != 0) usleep(polling_interval);
	}

	rtnl_close(&rth);

	// 获取结束时间并统计打印
	if (gettimeofday(&stop, NULL) == -1) {
		perror("gettimeofday");
		return 1;
	}
	delta =
			(stop.tv_sec - start.tv_sec) * 1000000000 + (stop.tv_usec - start.tv_usec) * 1000;
	printf("average latency: %li ns\n", delta / (count * 2));
  // 测速结束

	return ret;
}

// 简化测试命令为： make && ./tc/tc -s show
// 实际执行： default run command: tc -d -s class show dev s2-eth1
void my_show_queue_argv(int *argc, char **argv){
	// &argc=5;
	++show_stats; // turn on stats 默认打开统计， 不需要 -s参数
	// ++show_details; #turn of detail
	//char *av=
	argv[1]="class";
	argv[2]="show";
	argv[3]="dev";
	argv[4]="s2-eth2";
}

// 简化测试命令为： make && ./tc/tc -s change
// 实际执行： sudo tc class change dev s2-eth2 parent 1:fffe classid 1:2 htb rate 2mbit burst 15k
void my_change_queue_argv(int *argc, char **argv){
	// &argc=5;
	++show_stats; // turn on stats 默认打开统计， 不需要 -s参数
	// ++show_details; #turn of detail
	//char *av=
	argv[1]="class";
	argv[2]="change";
	argv[3]="dev";
	argv[4]="s2-eth2";
	argv[5]="parent";
	argv[6]="1:fffe";
	argv[7]="classid";
	argv[8]="1:2";
	argv[9]="htb";
	argv[10]="rate";
	argv[11]="2mbit";
}
