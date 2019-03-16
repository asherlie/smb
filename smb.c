#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <dirent.h>

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
            if(strstr(inf->d_name, sterm)){
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
      printf("usage:\n  %s -C [board_name] - creates a board with name [board_name]\n"
                     "  %s [board_path]    - joins board at path [board_path]\n",
      bname, bname);
}

int main(int a, char** b){
      /* TODO: should we join the first .smbr we can find in this case? */
      if(a == 1){
            p_usage(*b);
            return 1;
      }
      _Bool lim_pwd = 0, create = 0;
      int cre_arg;
      for(int i = 1; i < a; ++i){
            if(*b[i] == '-'){
                  /* pwd mode */
                  if(b[i][1] == 'p'){
                        lim_pwd = 1;
                        continue;
                  }
                  /* host/create mode */
                  if(i < a-1 && b[i][1] == 'C'){
                        create = 1;
                        cre_arg = i+1;
                  }
            }
      }
      if(create){
            char ext[PATH_MAX] = {0};
            snprintf(ext, PATH_MAX,
            (strchr(b[cre_arg], '/') || lim_pwd) ? "%s.smbr" : "/var/tmp/%s.smbr",
            b[cre_arg]);
            create_mb(ext);
            /* create_mb shouldn't return */
            puts("failed to create mb");
            return 1;
      }
      /* client mode */
      /* sc_dir first searhces /var/tmp and them /tmp
       * for board files matching sterm
       */
      char* board = sc_dir(".", b[1], 5, ".smbr");
      if(!board){
            if(lim_pwd)goto failure;
            board = sc_dir("/var/tmp", b[1], 5, ".smbr");
      }
      if(!board){
            failure:
            printf("no board matching %s was found\n", b[1]);
            return 1;
      }
      client(board);
      return 0;
}
