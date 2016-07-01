
void bench_cycle(){ // tc.c mian()
	// 测速循环
	for (i=0; i<count; i++){
		if (polling_interval != 0) usleep(polling_interval);
		ret = do_cmd(argc-1, argv+1); // 禁止测速循环运行， 测速时再打开

	}

}


// 旧的参数初始化代码 与 helper() 函数
// argc=5; my_show_queue_argv(&argc, argv);  	// class show 命令初始化
// argc=12; my_change_queue_argv(&argc, argv); // class change 命令初始化
// printf("debug: default %d argvs: %s %s %s %s\n", argc, argv[1], argv[2], argv[3], argv[4]);
// ret = do_cmd(argc-1, argv+1); //旧的参数传入 tc class show
// return ret;

// 简化测试命令为： make && ./tc/tc -s show
// 实际执行： default run command: tc -d -s class show dev s2-eth1
// 参数初始化 help代码
void my_show_queue_argv(int *argc, char **argv){ // tc.c mian()
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
