#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <termios.h>

#include "raw.h"

struct termios def, raw;

void raw_mode(){
      tcsetattr(0, TCSANOW, &raw);
}

/* reset_term can only be called after getline_raw has been */
void reset_term(){
      tcsetattr(0, TCSANOW, &def);
}

/* reads a line from stdin until an \n is found or a tab is found 
 * returns NULL on ctrl-c
 */
char* getline_raw(int* bytes_read, _Bool* tab, int* ignore){
      tcgetattr(0, &raw);
      tcgetattr(0, &def);
      cfmakeraw(&raw);
      raw_mode();
      char c;

      int buf_sz = 2;
      char* ret = calloc(buf_sz, 1);

      *tab = (*bytes_read = 0);

      while((c = fgetc(stdin)) != '\r'){
            if(ignore){
                  for(int* i = ignore; *i > 0; ++i){
                        if(c == *i)continue;
                  }
            }
            if(c == 3){
                  free(ret);
                  ret = NULL;
                  break;
            }
            /* if tab is detected */
            if(c == 9){
                  *tab = 1;
                  break;
            }
            /* delete */
            if(c == 127){
                  if(*bytes_read == 0)continue;
                  ret[--(*bytes_read)] = 0;
                  printf("\r%s%c\r%s", ret, ' ', ret);
                  continue;
            }
            if(*bytes_read == buf_sz){
                  buf_sz *= 2;
                  char* tmp_s = calloc(buf_sz, 1);
                  memcpy(tmp_s, ret, *bytes_read);
                  free(ret);
                  ret = tmp_s;
            }
            ret[(*bytes_read)++] = c;
            putchar(c);
      }
      /* before exiting, we restore term to its
       * default settings
       */
      reset_term();

      return ret;
}

/* data_offset is offset into data where char* can be found
 * this is set to 0 if data_douplep is a char*
 */
/* tab_complete behaves like getline(), but does not include \n char in returned string */
/* *free_s is set to 1 if returned buffer should be freed */
char* tab_complete(void* data_douplep, int data_blk_sz, int data_offset, int optlen,
                   char iter_opts, int* bytes_read, _Bool* free_s){
      _Bool tab, found_m;
      /* this should be called until enter is sent
       * results should be appeded to a master string
       */
      char* ret = getline_raw(bytes_read, &tab, NULL), * tmp_ch;
      *free_s = 0;
      if(tab && data_douplep){
            found_m = 0;
            _Bool select = 0;
            int maxlen = *bytes_read, tmplen;
            while(!select){
                  for(int i = 0; i <= optlen; ++i){
                        /* we treat i == optlen as input string */
                        if(i == optlen)tmp_ch = ret;
                        else{
                              void* inter = ((char*)data_douplep+(i*data_blk_sz)+data_offset);

                              /* can't exactly remember this logic -- kinda hard to reason about */
                              if(data_blk_sz == sizeof(char*))tmp_ch = *((char**)inter);
                              else tmp_ch = (char*)inter;
                        }
                        if(strstr(tmp_ch, ret)){
                              found_m = 1;

                              /* printing match to screen and removing chars from * old string */
                              tmplen = (tmp_ch == ret) ? *bytes_read : (int)strlen(tmp_ch);
                              putchar('\r');
                              printf("%s", tmp_ch);
                              if(tmplen > maxlen)maxlen = tmplen;
                              for(int i = 0; i < maxlen-tmplen; ++i)putchar(' ');
                              putchar('\r');

                              raw_mode();

                              char ch;
                              while(((ch = getc(stdin)))){
                                    if(ch == '\r'){
                                          *bytes_read = tmplen;
                                          if(ret != tmp_ch){
                                                free(ret);
                                                *free_s = 0;
                                          }
                                          ret = tmp_ch;
                                          select = 1;
                                          break;
                                    }
                                    if(ch == iter_opts)break;
                              }

                              reset_term();

                              if(select)break;
                              continue;
                        }
                        /* TODO: in this case, allow user to enter more chars */
                        else if(i == optlen-1 && !found_m){
                              select = 1;
                              break;
                        }
                  }
            }
      }
      return ret;
}
