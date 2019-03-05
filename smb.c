#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>

#include <sys/socket.h>
#include <sys/un.h>

#define MSG_CREATE_BOARD 0

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
int listen_sock(){
      int sock = socket(AF_UNIX, SOCK_STREAM, 0);
      return sock;
}

int mb_handler(int mb_type, char* str_arg){
}

/* creates an mb in the working directory */
void create_mb(char* name){
      // FILE* fp = fopen(name, "w");
      pid_t pid = fork();
      if(pid > 0)exit(EXIT_SUCCESS);
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
      int mb_type; char str_buf[201];

      while(1){
            memset(str_buf, 0, 201);
            peer_sock = accept(sock, NULL, NULL);
            read(peer_sock, &mb_type, sizeof(int));
            read(peer_sock, str_buf, 200);
      }
}

int main(int a, char** b){
      create_mb("");
      (void)a; (void)b;
      return 0;
}
