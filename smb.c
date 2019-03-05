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
      return -1;
}

int main(int a, char** b){
      (void)a; (void)b;
      return 0;
}
