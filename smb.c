#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <limits.h>
#include <sys/stat.h>

#include <sys/socket.h>
#include <sys/un.h>

/*
 *#define MSG_CREATE_BOARD  0
 *#define MSG_REMOVE_BOARD  1
 */
#define MSG_CREATE_THREAD 2
#define MSG_REMOVE_THREAD 3
#define MSG_REPLY_THREAD  4

/* 
 * smb
 * [s]ecure [m]essage [b]oard
 *
 *           or
 *
 * [s]erver [m]essage [b]oard
 *
 *           or
 *
 * [s]imple [m]essage [b]oard
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * a new mb can be created by running smb with 
 * the -C flag followed by board name
 * this will create a unix socket file by the 
 * name `board_name`.smb, which must be known
 * by all who want to connect to the board
 *
 *   ex:
 *     ./smb -C thinkpad
 *
 * to log on to a board smb can be run without
 * any flags with the relevant unix socket file
 * as argument
 *
 *   ex:
 *     ./smb thinkpad.smb
 *
 */

/* returns a new sock listening for connections */

struct mb_msg{
      int mb_inf[2];
      char str_arg[201];
};

int listen_sock(){
      int sock = socket(AF_UNIX, SOCK_STREAM, 0);
      return sock;
}

/* host code */

uid_t get_peer_cred(int p_sock){
      uid_t uid;
      #ifdef SO_PEERCRED
      socklen_t len = sizeof(struct ucred);
      struct ucred cred;
      memset(&cred, 0, sizeof(struct ucred));
      getsockopt(p_sock, SOL_SOCKET, SO_PEERCRED, &cred, &len);
      uid = cred.uid;
      #else
      gid_t gid;
      getpeereid(p_sock, &uid, &gid);
      #endif
      return uid;
}

_Bool mb_handler(int mb_type, int ref_no, char* str_arg){
      switch(mb_type){
            case MSG_CREATE_THREAD:
                  puts("thread created");
                  break;
            case MSG_REMOVE_THREAD:
                  /* only she who created a thread can delete it */
                  printf("thread %i removed\n", ref_no);
                  break;
            default: return 0;
      }
      printf("str arg: %s recvd\n", str_arg);
      return 1;
}

/* creates an mb in the working directory */
void create_mb(char* name){
      /* checking for existence of socket */
      struct stat st;
      memset(&st, 0, sizeof(struct stat));
      stat(name, &st);
      if(st.st_ino != 0){
            printf("mb: \"%s\" already exists\n", name);
            return;
      }
      pid_t pid = fork();
      if(pid > 0){
            printf("mb spawned at pid: %i\n", pid);
            exit(EXIT_SUCCESS);
      }
      int sock = listen_sock();
      if(sock == -1)return;

      struct sockaddr_un addr;
      memset(&addr, 0, sizeof(struct sockaddr_un));
      addr.sun_family = AF_UNIX;
      strncpy(addr.sun_path, name, sizeof(addr.sun_path));

      if(bind(sock, (struct sockaddr*)&addr, SUN_LEN(&addr)) == -1
      || listen(sock, 0) == -1)return;

      int peer_sock = -1;
      /* recvd info */
      /* str_buf contains up to 200 chars */
      int mb_inf[2] = {-1, -1}; char str_buf[201];

      while(1){
            memset(str_buf, 0, 201);
            peer_sock = accept(sock, NULL, NULL);
            read(peer_sock, mb_inf, sizeof(int)*2);
            read(peer_sock, str_buf, 200);

            mb_handler(mb_inf[0], mb_inf[1], str_buf);
      }
}

/* end host code */

/* client code */

int send_mb_r(struct mb_msg mb_a, int sock){
      int ret = 1;
      ret &= send(sock, mb_a.mb_inf, sizeof(int)*2, 0);
      ret &= send(sock, mb_a.str_arg, 200, 0);
      return ret;
}

/* returns thread ref no */
int create_thread(char* th_name, int sock){
      struct mb_msg mb_a;
      mb_a.mb_inf[0] = MSG_CREATE_THREAD;
      mb_a.mb_inf[1] = -1;
      memset(mb_a.str_arg, 0, 201);
      strncpy(mb_a.str_arg, th_name, 200);
      return send_mb_r(mb_a, sock);
}

int reply_thread(int th_ref_no, char* msg, int sock){
      struct mb_msg mb_a;
      mb_a.mb_inf[0] = MSG_REPLY_THREAD;
      mb_a.mb_inf[1] = th_ref_no;
      memset(mb_a.str_arg, 0, 201);
      strncpy(mb_a.str_arg, msg, 200);
      return send_mb_r(mb_a, sock);
}

_Bool client(char* sock_path){
      int sock = listen_sock();
      struct sockaddr_un r_addr;
      memset(&r_addr, 0, sizeof(struct sockaddr_un));
      r_addr.sun_family = AF_UNIX;
      strncpy(r_addr.sun_path, sock_path, sizeof(r_addr.sun_path));

      if(connect(sock, (struct sockaddr*)&r_addr, sizeof(struct sockaddr_un)) == -1)
            return 0;

      int cur_thread = -1;

      char* inp = NULL, * tmp_p;
      size_t sz = 0;
      int b_read;

#if 0
how should the client print messages? using sockets or reading from text file?
leaning towards sockets
each new thread needs to be spread to every user
host could spawn a notify thread each time
iterates thru a quick -1 terminated arr of sockets
passed as the pthread arg
sends a message to each contaning ref num and 200 chars
each thread in client now needs to listen() and spawn a thread to read()
no need to accept() - this is handled by host
just continuously read() an int and 200 char str
with each new read(), we add thread ref num and name to our struct
host doesn't even need a complex struct, come to think of it, it can just
authenticate users and send out thread ref nums and names
this struct can be built client-side 

maybe don't even need struct, each client can just read() and if cur_thread
!= ref_no, don't print anything
otherwise, print uid_t and message

all switch thread does is change cur_thread
it will lookup ref_num from thread name string
the struct that stores thread names and ref_nums
is the only necessary struct - it will be mutex locked
and shared between the client read thread and client while loop below

TODO: write notify function compatible with pthread_create:
      maintain peer sock array from host side

TODO: write thread name ref_num struct:
      char* th_name; int ref_num;
      pthread_mutex_lock lck;

TODO: spawn a read() thread for each client:
      thread should update the above struct
#endif

      while((b_read = getline(&inp, &sz, stdin)) != EOF){
            inp[--b_read] = 0;
            if(*inp == '/' && b_read > 1){
                  switch(inp[1]){
                        case 't':
                              /* switch threads */
                              ;
                        case 'c':
                              if((tmp_p = strchr(inp, ' ')))
                                    create_thread(tmp_p+1, sock);
                              break;
                  }
            }
      }
      return 1;
}

/* end client code */

/* TODO: is this more of a chatroom? */
int main(int a, char** b){
      if(a == 1){
            printf("usage: %s ... \n", *b);
            return 1;
      }
      for(int i = 1; i < a-1; ++i){
            if(*b[i] == '-' && b[i][1] == 'C'){
                  /* b[i+1] will always exist */
                  create_mb(b[i+1]);
                  puts("failed to create mb");
                  return 1;
            }
      }
      /* client mode */
      client(b[1]);
      return 0;
}
