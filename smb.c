#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <limits.h>
#include <pthread.h>

#include "host.h"
#include "client.h"

/* 
 * smb
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
