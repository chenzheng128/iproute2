#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

char socket_path[128] = "/tmp/sdn.socket";
//char *socket_path = "/tmp/sdn.socket";
// char *socket_path = "\0hidden";

int DEBUG = 0;      //1- 打开debug   0- 关闭debug
int BENCH_MODE = 0; //1- 打开测速模式 0- 关闭
char BENCH_CMD[] = "class show dev s2-eth1"; //1- 测速命令

#define LOG_INFO(format, ...) if(!BENCH_MODE) {fprintf(stdout, format, __VA_ARGS__);};
void log_info(char *str){
  if (BENCH_MODE)
    printf(str);
}
int main(int argc, char *argv[]) {
  int64_t i;        //临时循环变量
  struct sockaddr_un addr;
  char buf[1024];   // 回写缓存
  int fd;           //文件描述符
  int rc;           //read count


  if (argc <=1) {
    printf("Usage: tc <socketname> #普通模式 (监听 VAR_DIR/<socketname>.socket)\n");
    printf("Usage: tc bench # 测速模式 (监听 bench.socket)\n");
    exit(1);
  }
  if (argc > 1) {
    //使用命令行参数建立unix socket
    char VAR_DIR[]="/var/sdn/";
    memset(&socket_path, 0, sizeof(socket_path)); //
    strcpy(socket_path, VAR_DIR);
  	strcat(socket_path, argv[1]);
  	strcat(socket_path, ".socket");
  	printf ("debug: build socket_path: %s\n", socket_path);
    if (strcmp(argv[1], "bench")==0) {
      BENCH_MODE=1;
      printf ("debug: entering BENCH_MODE, supressing all output ...\n");

    }
  }

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

  printf ("debug: connet on socket_path: %s\n", socket_path);
  if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
    perror("connect error");
    exit(-1);
  }

  // 测速相关变量定义
	//int64_t bench_total = 5; //查询次数
	int64_t bench_total=1000000; //测速 循环数 average latency: 3506 ns (4us) / real	0m7.015s / CPU 100%
	unsigned int polling_interval=0; 			//采样间隔时间, 默认0，不休眠
  polling_interval =  20; bench_total=100000; //average latency: 79083 ns avg_bytes(should equals to interactive mode): 247
	//polling_interval =  1; bench_total=100000; 	// 增加采样间隔 1微秒 ; 37-42us / real	0m7.447s / CPU 1-2%
	//polling_interval = 10; bench_total=100000; 	// 增加采样间隔10微秒 ; 47-50us / real	0m9.412s / CPU 1-2%
	// #ifdef HAS_CLOCK_GETTIME_MONOTONIC //linux 不用这个定义
	// 测速开始： 定义时间变量， 获取初始时间


	int64_t  delta;
  struct timeval start, stop;
	if (gettimeofday(&start, NULL) == -1) {
      perror("gettimeofday");
      return 1;
  }

  int readcount = 0;
  int bench_count = 0;
  int64_t total_bytes = 0;
  while(1) {
    if (!BENCH_MODE ) { // 调整此判断以激活 交换模式 或 测速模式
      // 交互模式， 从 STDIN_FILENO 读取命令
      if ((rc=read(STDIN_FILENO, buf, sizeof(buf))) <= 0) break; //blocking wating here for STDIN command
    }
    else{ // 测速模式： 假设从终端读入的是 BENCH_CMD
      rc = strlen(BENCH_CMD);
      strcpy(buf,  BENCH_CMD);
      //printf("%s\n", buf);
    }

    if (write(fd, buf, rc) != rc) { //写入错误提示
      if (rc > 0) fprintf(stderr,"partial write");
      else { perror("write error"); exit(-1); }
    }

    // 正常写入
    // 测速变量与延时控制
    bench_count++;        //每write一次命令， bench count++
    if(bench_count > bench_total) break; // 测速结束
    //printf("bench_count %d bench_total %d\n", bench_count, bench_total);
    if(polling_interval != 0) usleep(polling_interval);

    if ((readcount=read(fd,buf,sizeof(buf))) > 0){
        // TODO: [ svr echo length]: 327 bytes
        // 如果 readcount 和 交互模式下的获取 327 字符不一样，说明有数据还没完全读完，应该再读
        // 和缓存大小有关，可能会出现不对齐的现象
        // 但后面计算平均bytes应该接近于交换模式
        total_bytes += readcount;

        if (BENCH_MODE) {
          // 每 100个包 抽样打印 . 字符
          if ((bench_count % 100) == 0) printf("."); //got simple output for bench
          // 每 1000个包 抽样打印 60 字符
          int print_len=60;
          if ((bench_count % 1000) == 0) printf("[ svr echo length]: %u bytes\n[ svr echo content ]: %.*s \n", readcount, print_len, buf);
        }
        else //print all detail output
          LOG_INFO("[ svr echo length]: %u bytes\n[ svr echo content ]: %.*s \n", readcount, readcount-1, buf);

        // %.*s 表示打印buf 的 readcount 长度; 如果不设定readcount长度直接打印 %s;
        // 就应该调用后面的 memset 对buf 清零,
        // 而是会因为buf中的字符不会结束在 \0 而打印出一些剩余内容否则
    }
    memset(&buf, 0, sizeof(buf)); //每次结束时注意清零buf, 便于字符串正常结束
    if (!BENCH_MODE) LOG_INFO("plearse input your command: %s","");
    fflush(0);

  }


  if (BENCH_MODE) {
    printf("polling_interval: %li\n", polling_interval);
    printf("roundtrip count: %li\n", bench_total);
    printf("total_bytes: %li\n", total_bytes);
    // 平均bytes应该接近于交换模式, 这里是 247 说明还是少读了一些包
    printf("avg_bytes(should equals to interactive mode): %li bytes \n", total_bytes/bench_total);
  }

	// 获取结束时间并统计打印
	if (gettimeofday(&stop, NULL) == -1) {
		perror("gettimeofday");
		return 1;
	}
	delta =
			(stop.tv_sec - start.tv_sec) * 1000000000 + (stop.tv_usec - start.tv_usec) * 1000;

	if (BENCH_MODE)  printf("average latency: %li ns\n", delta / (bench_total * 2));
  // 测速结束

  return 0;
}
