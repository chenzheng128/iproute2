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

#include <sys/socket.h>
#include <sys/un.h>

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


/* 新的参数处理代码 */
static int setargs(char *args, char **argv)
{
   int count = 0;

   while (isspace(*args)) ++args;
   while (*args) {
     if (argv) argv[count] = args;
     while (*args && !isspace(*args)) ++args;
     if (argv && *args) *args++ = '\0';
     while (isspace(*args)) ++args;
     count++;
   }
   return count;
}

char **parsedargs(char *args, int *argc)
{
   char **argv = NULL;
   int    argn = 0;

   if (args && *args
    && (args = strdup(args))
    && (argn = setargs(args,NULL))
    && (argv = malloc((argn+1) * sizeof(char *)))) {
      *argv++ = args;
      argn = setargs(args,argv);
   }

   if (args && !argv) free(args);

   *argc = argn;
   return argv;
}

void freeparsedargs(char **argv)
{
  if (argv) {
    free(argv[-1]);
    free(argv-1);
  }
}


// 定义全局变量
int DEBUG = 0;			//打印输出
int CLIENT_SOCK_FD = 0; 	//全局变量FD, 用于其他程序write(CLIENT_SOCK_FD)回客户端
int ECHO_TO_SERVER = 0; //控制命令class show dev s1-eth3输出位置 输出在 1 - 服务端  0 -客户端
char VAR_DIR[]="/var/sdn/";

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

	// 新的参数初始化变量
	show_stats=1; //总是打开统计输出 ( 默认 -s)
	int64_t i;
	char **argv_new;
	int argc_new;
	char *input_cmd_str = NULL;


	// 建立 socket 监听通道 从 socket 通道解析传入命令 并进行参数处理
	// #define SOCK_PATH "/dev/socket/echo_socket"
	//char *socket_path = "\0hidden";
	// char *socket_path = "/tmp/sdn.socket";
	char socket_path[128];
	strcpy(socket_path, VAR_DIR);
	strcat(socket_path, argv[1]);
	strcat(socket_path, ".socket");
	printf ("debug: build socket_path: %s\n", socket_path);
	struct sockaddr_un addr;
  	int fd;

  	// if (argc > 1) socket_path=argv[1];

  	if ( (fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
    	perror("socket error");
    	exit(-1);
  	}

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	if (*socket_path == '\0') {
		*addr.sun_path = '\0';
		strncpy(addr.sun_path+1, socket_path+1, sizeof(addr.sun_path)-2);
	} else {
		strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path)-1);
	}
	unlink(socket_path);

	printf ("debug: binding on socket_path: %s\n", socket_path);
  	if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
    	perror("bind error");
    	exit(-1);
  	}

	if (listen(fd, 5) == -1) {
		perror("listen error");
		exit(-1);
	}

	int cl,readcount;
	char buf[128];
  	while (1) {
		printf("debug: wating new client to accetp()...\n");
	    if ( (cl = accept(fd, NULL, NULL)) == -1) {
	    	perror("accept error");
	    	continue;
	    }
		CLIENT_SOCK_FD = cl;

		char input_str[128]; // 读入socket传来的控制命令
		int	 wcount = 0;
		printf("debug: wating new client to read()...\n");
	    while ( (readcount=read(cl,buf,sizeof(buf))) > 0) {
			//if (readcount<2){
			//	printf("illegal command strlen<2");
			//	continue;
			//}
	      	printf("debug: read %u bytes: %.*s\n", readcount, readcount, buf);

			memcpy(input_str, buf, readcount); input_str[readcount]='\0'; //复制字符串并添加结束字符

			argv_new = parsedargs(input_str,&argc_new);

			printf("== debug: default %d argvs \n",argc_new);
			for (i = 0; i < argc_new; i++)
				printf("[%s] ",argv_new[i]);
			printf("\n");

			ret = do_cmd(argc_new, argv_new); //新的参数处理传入 class show
			freeparsedargs(argv_new); //释放 新参数 的内存占用

			// TODO 这里一定要写回数据, 否则client再发数据来也接受不到, 为什么?
			if ((wcount=write(cl, buf, readcount)) != readcount) { // echo 单行缓存
	          	if (wcount > 0) fprintf(stderr,"partial write %d \n", wcount);
	          	else {
	            	perror("write error");
	            	exit(-1);
          		}
      		}
	    }
	    if (readcount == -1) {
	      	perror("read");
	      	exit(-1);
	    }
	    else if (readcount == 0) { // client close 会话
	      	printf("EOF\n");
	      	close(cl);
	    }
  	}

	
	rtnl_close(&rth);

	return ret;
}
