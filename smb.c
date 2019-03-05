#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <limits.h>
#include <sys/stat.h>

#include <sys/socket.h>
#include <sys/un.h>

#define MSG_CREATE_BOARD  0
#define MSG_REMOVE_BOARD  1
#define MSG_CREATE_THREAD 2
#define MSG_REMOVE_THREAD 3

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

uid_t get_peer_cred(int sock){
}

_Bool mb_handler(int mb_type, int ref_no, char* str_arg){
      switch(mb_type){
            case MSG_CREATE_BOARD:
                  puts("board created");
                  break;
            case MSG_REMOVE_BOARD:
                  printf("board %i removed\n", ref_no);
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

/* returns board ref no */
int create_board(char* b_name, int sock){
      struct mb_msg mb_a;
      mb_a.mb_inf[0] = MSG_CREATE_BOARD;
      mb_a.mb_inf[1] = -1;
      memset(mb_a.str_arg, 0, 201);
      strncpy(mb_a.str_arg, b_name, 200);
      send_mb_r(mb_a, sock);
      return -1;
}

/* returns thread ref no */
int create_thread(){
      return -1;
}

_Bool client(char* sock_path){
      int sock = listen_sock();
      struct sockaddr_un r_addr;
      memset(&r_addr, 0, sizeof(struct sockaddr_un));
      r_addr.sun_family = AF_UNIX;
      strncpy(r_addr.sun_path, sock_path, sizeof(r_addr.sun_path));

      if(connect(sock, (struct sockaddr*)&r_addr, sizeof(struct sockaddr_un)) == -1)
            return 0;

      // char* inp = NULL;
      return 1;
}

/* end client code */

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
