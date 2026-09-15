#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>          
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <sqlite3.h>
#include <signal.h>
#include <time.h>


#define   N  32

#define  R  1   // user - register
#define  L  2   // user - login
#define  Q  3   // user - query
#define  H  4   // user - history

#define  DATABASE  "my.db"

// 信息结构体
typedef struct {
	int type;
	char name[N];
	char data[256];
}MSG;



int do_client(int acceptfd, sqlite3 *db);
void do_register(int acceptfd, MSG *msg, sqlite3 *db);
int do_login(int acceptfd, MSG *msg, sqlite3 *db);
int do_query(int acceptfd, MSG *msg, sqlite3 *db);
int do_history(int acceptfd, MSG *msg, sqlite3 *db);
int history_callback(void* arg,int f_num,char** f_value,char** f_name);
int do_searchword(int acceptfd, MSG *msg, char word[]);
int get_date(char *date);


//./server 0.0.0.0 8888
int main(int argc, const char *argv[])
{

	int sockfd;
	struct sockaddr_in  serveraddr;
	int n;
	MSG  msg;
	sqlite3 *db;
	int acceptfd;
	pid_t pid;

	if(argc != 3)
	{
		printf("Usage:%s serverip  port.\n", argv[0]);
		return -1;
	}

	//打开数据库
	if(sqlite3_open(DATABASE, &db) != SQLITE_OK)
	{
		printf("%s\n", sqlite3_errmsg(db));
		return -1;
	}
	else
	{
		printf("open DATABASE success.\n");

	}

	if((sockfd = socket(AF_INET, SOCK_STREAM,0)) < 0)
	{
		perror("fail to socket.\n");
		return -1;
	}

	bzero(&serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.s_addr = inet_addr(argv[1]);
	serveraddr.sin_port = htons(atoi(argv[2]));

	if(bind(sockfd, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) < 0)
	{
		perror("fail to bind.\n");
		return -1;
	}
	
	//设置socket
	if(listen(sockfd, 5) < 0)
	{
		printf("fail to listen.\n");
		return -1;
	}

	//处理僵尸进程
	signal(SIGCHLD, SIG_IGN);

	while(1)
	{
		if((acceptfd = accept(sockfd, NULL, NULL)) < 0)
		{
			perror("fail to accept");
			return -1;
		}

		if((pid = fork()) < 0)
		{
			perror("fail to fork");
			return -1;
		}
		else if(pid == 0)  // 儿子进程
		{
			close(sockfd);
			do_client(acceptfd, db);

		}
		else  
		{
			close(acceptfd);
		}
	}
	
	return 0;
}


int do_client(int acceptfd, sqlite3 *db)
{
	MSG msg;
	while(recv(acceptfd, &msg, sizeof(msg), 0) > 0)
	{
	  printf("type:%d\n", msg.type);
	   switch(msg.type)
	   {
	  	 case R:
			 do_register(acceptfd, &msg, db);
			 break;
		 case L:
			 do_login(acceptfd, &msg, db);
			 break;
		 case Q:
			 do_query(acceptfd, &msg, db);
			 break;
		 case H:
			 do_history(acceptfd, &msg, db);
			 break;
		 default:
			 printf("Invalid data msg.\n");
	   }

	}

	printf("client exit.\n");
	close(acceptfd);
	exit(0);

	return 0;
}

void do_register(int acceptfd, MSG *msg, sqlite3 *db)
{
	char * errmsg;
	char sql[128];

	sprintf(sql, "insert into usr values('%s', %s);", msg->name, msg->data);
	printf("%s\n", sql);

	if(sqlite3_exec(db,sql, NULL, NULL, &errmsg) != SQLITE_OK)
	{
		printf("%s\n", errmsg);
		strcpy(msg->data, "usr name already exist.");
	}
	else
	{
		printf("client  register success!\n");
		strcpy(msg->data, "client register success");
	}

	if(send(acceptfd, msg, sizeof(MSG), 0) < 0)
	{
		perror("send");
		return ;
	}

	return ;
}

int do_login(int acceptfd, MSG *msg , sqlite3 *db)
{
	char sql[128] = {};
	char *errmsg;
	int nrow;
	int ncloumn;
	char **resultp;

	sprintf(sql, "select * from usr where name = '%s' and pass = '%s';", msg->name, msg->data);
	printf("%s\n", sql);

	if(sqlite3_get_table(db, sql, &resultp, &nrow, &ncloumn, &errmsg)!= SQLITE_OK)
	{
		printf("%s\n", errmsg);
		return -1;
	}
	else
	{
		printf("sqlite3 get_table success!\n");
	}

	// 成功
	if(nrow == 1)
	{
		strcpy(msg->data, "login success");
		send(acceptfd, msg, sizeof(MSG), 0);
		return 1;
	}


	// 密码或者用户名错误
	if(nrow == 0) 

	{
		strcpy(msg->data,"usr name or passwd wrong.");
		send(acceptfd, msg, sizeof(MSG), 0);
	}

	return 0;
}

int do_searchword(int acceptfd, MSG *msg, char word[])
{
	FILE * fp;
	int len = 0;
	char temp[512] = {};
	int result;
	char *p;


//打开dict查找	
	if((fp = fopen("dict.txt", "r")) == NULL)
	{
		perror("fopen");
		strcpy(msg->data, "Failed to open dict.txt");
		send(acceptfd, msg, sizeof(MSG), 0);
		return -1;
	}

	len = strlen(word);
	printf("%s , len = %d\n", word, len);


	//找单词
	while(fgets(temp, 512, fp) != NULL)
	{


		result = strncmp(temp,word,len);

		if(result < 0)
		{
			continue;
		}
		if(result > 0 || ((result == 0) && (temp[len]!=' ')))
		{
			break;
		}

		// 找到了
		p = temp + len; 

		while(*p == ' ')
		{
			p++;
		}

		// 跃过所有的空格
		strcpy(msg->data, p);
		printf("found word:%s\n", msg->data);

		fclose(fp);
		return 1;
	}

	fclose(fp);

	return 0;
}

int get_date(char *date)
{
	time_t t;
	struct tm *tp;

	time(&t);

	//进行时间格式转换
	tp = localtime(&t);

	sprintf(date, "%d-%d-%d %d:%d:%d", tp->tm_year + 1900, 
			tp->tm_mon+1,
			tp->tm_mday, 
			tp->tm_hour,
			tp->tm_min,
			tp->tm_sec);

	printf("get date:%s\n", date);

	return 0;
}

int do_query(int acceptfd, MSG *msg , sqlite3 *db)
{
	char word[64];
	int found = 0;
	char date[128];
	char sql[128];
	char *errmsg;

	//查询的单词
	strcpy(word, msg->data);

	found = do_searchword(acceptfd, msg, word);
	printf("client search word success.\n");

	if(found == 1)
	{
		// 时间
		get_date(date);

        sprintf(sql, "insert into record values('%s', '%s', '%s')", msg->name, date, word);

		if(sqlite3_exec(db, sql, NULL, NULL, &errmsg) != SQLITE_OK)
		{
			printf("%s\n", errmsg);
			return -1;
		}
		else
		{
			printf("Insert record done.\n");
		}

	}
	else  
	{
		strcpy(msg->data, "the word is not found!");
	}

	send(acceptfd, msg, sizeof(MSG), 0);

	return 0;
}


int history_callback(void* arg,int f_num,char** f_value,char** f_name)
{
	int acceptfd;
	MSG msg;
//记录查询返回
	acceptfd = *((int *)arg);

	sprintf(msg.data, "%s , %s", f_value[1], f_value[2]);

	send(acceptfd, &msg, sizeof(MSG), 0);

	return 0;
}


int do_history(int acceptfd, MSG *msg, sqlite3 *db)
{
	char sql[128] = {};
	char *errmsg;

	sprintf(sql, "select * from record where name = '%s'", msg->name);

	if(sqlite3_exec(db, sql, history_callback,(void *)&acceptfd, &errmsg)!= SQLITE_OK)
	{
		printf("%s\n", errmsg);
	}
	else
	{
		printf("Query record done.\n");
	}

	msg->data[0] = '\0';

	send(acceptfd, msg, sizeof(MSG), 0);

	return 0;
}


