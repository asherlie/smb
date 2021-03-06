#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <dirent.h>
#include <signal.h>

#include "shared.h"
#include "host.h"
#include "client.h"


/* 
 * a new mb can be created by running smb with 
 * the -C flag followed by board name
 * this will create a unix socket file by the 
 * name `board_name`.smbr, which must be known
 * by all who want to connect to the board
 *
 *   ex:
 *     ./smb -C chat
 *
 * to log on to a board smb can be run without
 * any flags with the relevant unix socket file
 * as argument
 *
 *   ex:
 *     ./smb chat.smbr
 *
 */

char* sc_dir(char* path, char* sterm, int extlen, char* ext){
      DIR* dir = opendir(path);
      if(!dir)return NULL;
      struct dirent* inf;
      char* ret = NULL;
      int namelen;
      _Bool good;
      while((inf = readdir(dir))){
            if(!sterm || strstr(inf->d_name, sterm)){
                  if(((int)(namelen = strnlen(inf->d_name, PATH_MAX)) <= extlen))
                        continue;
                  good = 1;
                  for(int i = 0; i < extlen; ++i){
                        if(inf->d_name[namelen-i-1] != ext[extlen-i-1]){
                              good = 0;
                              break;
                        }
                  }
                  if(good){
                        ret = inf->d_name;
                        break;
                  }
            }
      }
      closedir(dir);
      char* ret_ext = NULL;
      if(ret){
            int pathlen = strnlen(path, PATH_MAX);
            _Bool sl = path[pathlen-1] != '/';
            ret_ext = malloc(pathlen+namelen+sl+1);
            snprintf(ret_ext, pathlen+namelen+sl+1, 
            (sl) ? "%s/%s" : "%s%s", path, ret);
      }
      return ret_ext;
}

void p_usage(char* bname){
      printf("usage:\n  %s {-[a]ny} {-[p]wd} {-[d]ur [hours]} {-C} [board_name]"
             " - creates or joins a board with name [board_name]\n\n"
             "  -C indicates create only mode\n"
             "  if board does not exist, even if -C is not passed, it is created\n\n"
             "  if the -pwd flag is set, the scope of smb's board creation\n"
             "  and searching is limited to the current working directory\n\n"
             "  if the -any flag is set, the first board that is found will be joined\n\n"
             "  if the -dur flag is used to set a board time limit, the board will be\n"
             "  removed after [hours] hours - otherwise, board will last for 5 days\n",
      bname);
}

int main(int a, char** b){
      _Bool lim_pwd = 0, create = 0, any = 0;
      int cre_arg = -1, dur = -1;
      for(int i = 1; i < a; ++i){
            if(*b[i] == '-'){
                  switch(b[i][1]){
                        case 'v':
                              printf("smb version %s with ashio version %s\n", SMB_VER, ASHIO_VER);
                              return 0;
                        case 'h':
                              p_usage(*b);
                              return 0;
                        /* pwd mode */
                        case 'p':
                              lim_pwd = 1;
                              break;
                        /* host/create mode */
                        case 'C':
                              if(i == a-1)break;
                              create = 1;
                              cre_arg = i+1;
                              break;
                        case 'a':
                              any = 1;
                              break;
                        /* TODO: allow setting duration with h:m for finer control */
                        case 'd':
                              /* if this is the last arg */
                              if(i == a-1)break;
                              strtoi(b[++i], &dur);
                              break;
                        /* cre_arg will be set to either to element after -C or
                         * the first non flag
                         */
                        default:
                              if(cre_arg == -1)
                                    cre_arg = i;
                  }
            }
            else if(cre_arg == -1)
                  cre_arg = i;
      }
      if(cre_arg == -1)any = 1;
      /* client mode */
      /* sc_dir first searhces /var/tmp and them /tmp
       * for board files matching sterm unless any is
       * specified, in which case the first found
       * board will be joined
       */
      if(!create){
            char* board;
            if((board = sc_dir(".", any ? NULL : b[cre_arg], 5, ".smbr")) ||
               (board = sc_dir("/var/tmp", any ? NULL : b[cre_arg], 5, ".smbr"))){
                  _Bool cli = client(board);
                  /* TODO: remove this - it should never happen with updated /d protocol */
                  /* removing the vestige of an old board */
                  if(!cli)remove(board);
                  free(board);
                  if(cli)return 0;
            }
      }
      if(cre_arg == -1){
            p_usage(*b);
            return 1;
      }
      /* if we didn't find a matching board we'll create it */
      /* if we've gotten here, !board */
      char ext[PATH_MAX] = {0};
      snprintf(ext, PATH_MAX,
      (strchr(b[cre_arg], '/') || lim_pwd) ? "%s.smbr" : "/var/tmp/%s.smbr",
      b[cre_arg]);

      /* we're going to be trying to create a board twice
       * second time is in case an orphan socket exists
       */
      int cmb;
      for(int i = 0; i < 2; ++i){

            /* create_mb returns 2 from client process */
            if((cmb = create_mb(ext, dur)) == 2){
                  if(create)return 0;

                  usleep(10000);
                  if(!client(ext))
                        printf("could not start client on %s\n", ext);
                  else return 0;
            }
            /* if sock already exists, remove it */
            else if(!cmb)remove(ext);

            /* if create_mb() returns anything but 2 or 0, something
             * has gone wrong, either with socket creation, or spawning threads
             * in either of these two cases, we'll need to exit
             * and it won't be worthwhile to retry creation
             */
            else break;
      }

      /* upon create_mb error 1 or -1 */
      return 1;
}
