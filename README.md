# Online Dictionary · 在线词典

一个基于 **Linux TCP Socket** 的 C 语言在线词典，采用客户端/服务端（C/S）架构。
服务端用 `fork` 为每个连接派生一个子进程，用户信息与查询历史持久化在 **SQLite** 中。

支持多客户端并发连接、用户注册与登录、单词查询、查询历史记录。

---

## 功能

- **注册**：把用户名与密码写入 `usr` 表（用户名主键，重复注册会被拒绝）
- **登录**：校验用户名与密码，成功后进入查询菜单
- **查询单词**：按单词在 `dict.txt` 中检索释义，命中则把「时间 + 单词」写入 `record` 表
- **历史记录**：读取当前用户的全部查询历史并逐条回传
- **并发**：`accept` 后 `fork()` 子进程处理，主进程继续监听；用 `signal(SIGCHLD, SIG_IGN)` 自动回收子进程，避免僵尸进程

---

## 运行环境

- Linux（依赖 `fork`、`signal`、POSIX Socket，**不能在 Windows 上直接编译**）
- gcc
- SQLite 3 开发库

```bash
# Debian / Ubuntu
sudo apt install gcc libsqlite3-dev sqlite3

# CentOS / RHEL
sudo yum install gcc sqlite-devel
```

---

## 编译

```bash
gcc server.c -o server -lsqlite3
gcc client.c -o client
```

> 仓库里**没有提交编译产物**（`server`、`client` 在 `.gitignore` 中），请在本机重新编译。
> 原先的两个可执行文件是在别的机器上编的，换环境后本来就跑不起来。

---

## 初始化数据库（必做，否则跑不起来）

这是最容易踩的坑：**代码只调用 `sqlite3_open("my.db")`，它只会创建一个空库文件，并不会建表。**
而 `my.db` 属于运行时文件，已被 `.gitignore` 忽略。所以新环境克隆下来直接运行，注册/登录都会报：

```
no such table: usr
```

第一次运行前，先建好表结构（与原库完全一致）：

```bash
sqlite3 my.db "
CREATE TABLE usr(name text primary key, pass text);
CREATE TABLE record(name text, data text, word text);
"
```

| 表 | 字段 | 说明 |
|---|---|---|
| `usr` | `name` (主键), `pass` | 用户账号，密码明文 |
| `record` | `name`, `data`, `word` | 查询历史，`data` 存时间戳字符串 |

> 已经有 `my.db` 且里面有数据时，**不要再执行上面的建表语句**，否则会报 `table usr already exists`，把结构冲掉有丢数据的风险。

---

## 运行

服务端和客户端都通过**命令行参数**接收 IP 与端口，且 `dict.txt` / `my.db` 都是**相对路径**，
所以必须在与 `dict.txt` 同级的目录下启动服务端。

```bash
# 终端 1 —— 启动服务端（IP 端口）
./server 0.0.0.0 8888

# 终端 2 —— 启动客户端（服务端 IP 端口）
./client 127.0.0.1 8888
```

启动成功时服务端会打印：

```
open DATABASE success.
```

---

## 使用流程

客户端启动后先进入主菜单：

```
-----------------------------------------------------------------
- 1.register          2.login              3.quit               -
-----------------------------------------------------------------
Please choose:
```

**1 → 注册**

```
Input name:tom
Input passwd:123456          # 注意：密码目前只支持纯数字，见「已知问题」
client register success
```

**2 → 登录**（成功后自动切换到查询菜单）

```
Input name:tom
Input passwd:123456
login success
```

**登录后的菜单**

```
-----------------------------------------------------------------
- 1.query_word         2.history_record         3.quit          -
-----------------------------------------------------------------
Please choose:
```

**1 → 查询单词**（输入 `#` 退出查询）

```
input the word of you want to query:abacus
n.frame with beads that slide along parallel rods, used for teaching numbers to children, and (in some countries) for counting
input the word of you want to query:#
```

**2 → 历史记录**

```
2026-9-15 11:52:3 , abacus
```

---

## 通信协议

客户端与服务端共用同一个结构体，**直接把内存按原始字节收发**：

```c
#define N 32

typedef struct {
    int  type;        // 消息类型
    char name[N];     // 用户名
    char data[256];   // 载荷：密码 / 单词 / 释义 / 提示信息
} MSG;
```

结构体总长 **292 字节**（`int` 4 + `name` 32 + `data` 256），每次 `send`/`recv` 一个完整的 `MSG`。

| `type` | 宏 | 方向 | 含义 |
|---|---|---|---|
| `1` | `R` | 客户端 → 服务端 | 注册，`name` + `data`(密码) |
| `2` | `L` | 客户端 → 服务端 | 登录，`name` + `data`(密码) |
| `3` | `Q` | 客户端 → 服务端 | 查询，`data` 为待查单词，服务端把释义写回 `data` |
| `4` | `H` | 客户端 → 服务端 | 历史，服务端逐条回传，最后发一条 `data[0]=='\0'` 表示结束 |

> ⚠️ 协议**没有做网络字节序（`htonl`/`ntohl`）转换**，也没有对齐/版本协商。
> 因此客户端与服务端必须编译运行在**相同架构、相同字节序**的机器上，否则解析会错乱。
>
> ⚠️ 结构中 `int` 大小为 4 字节、`name`/`data` 无填充，依赖编译器默认对齐。换编译器或加 `-fpack-struct` 之类选项会导致两端结构体大小不一致。

---

## 数据文件

### `dict.txt`

词库，**19661 条**，CRLF 换行，UTF-8，约 1.4 MB。每行格式为：

```
单词 + 填充空格 + 释义
```

单词部分**填充到第 17 列**对齐，例如：

```
a                indef art one
abacus           n.frame with beads that slide along parallel rods, ...
abandon          v.  go away from (a person or thing or place) ...
```

检索逻辑在 `do_searchword()`，有两个隐含前提：

1. **必须按字母序排列**——代码用 `strncmp` 比较，一旦发现当前行大于目标词就 `break`，乱序会漏查；
2. **单词后必须紧跟空格**（靠上面的列对齐保证）——代码用 `temp[len] != ' '` 判断是否精确匹配，用于区分 `a` 和 `abacus` 这类前缀词。

> 该文件在 `.gitattributes` 中被标记为 `-text`，git 不会对它的 CRLF 做任何换行符转换，以保证与本地字节级一致。

### `my.db`

SQLite 数据库文件，由 `sqlite3_open()` 在服务端启动时自动创建，属运行时文件，**不纳入版本管理**。

---

## 项目结构

```
.
├── server.c          # 服务端：监听、fork 并发、SQLite 读写、词典检索
├── client.c          # 客户端：命令行菜单与交互
├── dict.txt          # 词库数据（19661 条，CRLF）
├── .gitignore        # 忽略编译产物与运行时文件
└── .gitattributes    # 锁定换行符策略
```

服务端函数一览：

| 函数 | 作用 |
|---|---|
| `main` | 建库、建 socket、bind、listen，循环 accept + fork |
| `do_client` | 子进程主循环，按 `type` 分发请求 |
| `do_register` | 注册，`insert into usr` |
| `do_login` | 登录，`select * from usr where name=? and pass=?` |
| `do_query` | 查词并写历史记录 |
| `do_searchword` | 在 `dict.txt` 中按字母序检索 |
| `do_history` | 查历史，用 `history_callback` 逐条回传 |
| `get_date` | 生成时间戳字符串 |

---

## 已知问题

以下都是当前代码里客观存在的缺陷，记录在此供后续修复参考：

1. **注册密码只支持纯数字。** `do_register()` 拼 SQL 时密码没加引号：

   ```c
   sprintf(sql, "insert into usr values('%s', %s);", msg->name, msg->data);
   //                                          ^^^^ 少了单引号
   ```

   传入 `abc` 会被 SQLite 当成列名，报 `no such column: abc`，而服务端统一返回
   `usr name already exist.`，错误提示具有误导性。修正方式是改成 `'%s'`。

2. **存在 SQL 注入风险。** 用户名、密码、待查单词都是直接 `sprintf` 拼进 SQL 的，
   应改用 `sqlite3_prepare_v2` + `sqlite3_bind_text` 预处理绑定。

3. **密码明文存储。** `usr.pass` 未做任何哈希，建议至少改成加盐哈希。

4. **`do_login` 用 `nrow == 1` 判定成功。** 如果同名用户出现重复行（理论上被主键挡住了），
   会误判为登录失败，更稳妥的写法是 `nrow > 0`。

5. **`do_client` 未校验 `recv` 返回值。** 客户端异常断开时不会打印提示，只在子进程退出时输出 `client exit.`。

6. **`main` 中 `n` 变量未使用**，编译时会有 `-Wunused-variable` 警告。

7. **`do_searchword` 每次查询都重新 `fopen("dict.txt")` 并线性扫描。**
   词库大了会明显变慢，可考虑启动时载入内存或建索引。

---

## License

未指定。
