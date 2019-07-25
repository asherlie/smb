#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <termios.h>
#include <pthread.h>

#include "ashio.h"

struct termios def, raw;

void raw_mode(){
      tcsetattr(0, TCSANOW, &raw);
}

/* reset_term can only be called after getline_raw has been */
void reset_term(){
      tcsetattr(0, TCSANOW, &def);
}

/*
 * TODO: possibly implement python style tab completion
 * where they show up at the same time
 * i can check for screen width
 */

/* reads a line from stdin until an \n is found or a tab is found 
 * returns NULL on ctrl-c
 */
/* TODO: 
 * getline_raw() should assume that the terminal is in raw mode
 * it is too slow to put term in raw mode and back with each read
 * from stdin
 * especially when being used with tab_complete(), which requires
 * the terminal to be in raw mode as well
 */

char* getline_raw_internal(char* base, int baselen, int* bytes_read, _Bool* tab, int* ignore){
      tcgetattr(0, &raw);
      tcgetattr(0, &def);
      cfmakeraw(&raw);
      raw_mode();
      char c;

      int buf_sz = 2+baselen;
      char* ret = calloc(buf_sz, 1);

      *tab = 0;
      *bytes_read = (baselen && base) ? baselen : 0;

      /*
       * since in raw mode, we can prepend our base str
       * what if the base str has been deleted --
       * we need to add it to string in progress too
       */
      for(int i = 0; i < baselen; ++i){
            ret[i] = base[i];
            putc(base[i], stdout);
      }

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
            /* deletion */
            if(c == 8 || c == 127){
                  if(*bytes_read == 0)continue;
                  ret[--(*bytes_read)] = 0;
                  printf("\r%s%c\r%s", ret, ' ', ret);
                  continue;
            }
            /* buf_sz-1 to leave room for \0 */
            if(*bytes_read == buf_sz-1){
                  buf_sz *= 2;
                  char* tmp_s = calloc(buf_sz, 1);
                  memcpy(tmp_s, ret, *bytes_read);
                  free(ret);
                  ret = tmp_s;
            }
            ret[(*bytes_read)++] = c;
            ret[*bytes_read] = 0;
            putchar(c);
      }
      /* before exiting, we restore term to its
       * default settings
       */
      reset_term();

      return ret;
}

char* getline_raw(int* bytes_read, _Bool* tab, int* ignore){
      return getline_raw_internal(NULL, 0, bytes_read, tab, ignore);
}

/* tabcom operations */

struct tabcom* init_tabcom(struct tabcom* tbc){
      if(!tbc)tbc = malloc(sizeof(struct tabcom));
      tbc->n = 0;
      tbc->cap = 5;
      tbc->tbce = malloc(sizeof(struct tabcom_entry)*tbc->cap);
      return tbc;
}

void free_tabcom(struct tabcom* tbc){
      free(tbc->tbce);
}

/* data_offset is offset into data where char* can be found
 * this is set to 0 if data_douplep is a char*
 */
int insert_tabcom(struct tabcom* tbc, void* data_douplep, int data_blk_sz, int data_offset, int optlen){
      int ret = 1;
      if(tbc->n == tbc->cap){
            ++ret;
            tbc->cap *= 2;
            struct tabcom_entry* tmp_tbce = malloc(sizeof(struct tabcom_entry)*tbc->cap);
            memcpy(tmp_tbce, tbc->tbce, sizeof(struct tabcom_entry)*tbc->n);
            free(tbc->tbce);
            tbc->tbce = tmp_tbce;
      }

      tbc->tbce[tbc->n].data_douplep = data_douplep;
      tbc->tbce[tbc->n].data_blk_sz = data_blk_sz;
      tbc->tbce[tbc->n].data_offset = data_offset;
      tbc->tbce[tbc->n++].optlen = optlen;
      return ret;
}

struct tabcom_entry pop_tabcom(struct tabcom* tbc){
      return tbc->tbce[--tbc->n];
}

/* TODO: possibly add int* param that's set to n_entries */
/* returns a NULL terminated list strings */
char** find_matches(struct tabcom* tbc, char* needle){
      /* TODO: dynamically resize */
      int n_entries = 0;
      for(int i = 0; i < tbc->n; ++i)
            n_entries += tbc->tbce[i].optlen;
      char** ret = malloc(sizeof(char*)*(n_entries+2)), * tmp_ch;

      int sz = 0;
      for(int i = 0; i < tbc->n; ++i){
            for(int j = 0; j < tbc->tbce[i].optlen; ++j){
                  void* inter = ((char*)tbc->tbce[i].data_douplep+(j*tbc->tbce[i].data_blk_sz)+tbc->tbce[i].data_offset);

                  /* can't exactly remember this logic -- kinda hard to reason about */
                  if(tbc->tbce[i].data_blk_sz == sizeof(char*))tmp_ch = *((char**)inter);
                  else tmp_ch = (char*)inter;

                  if(strstr(tmp_ch, needle))ret[sz++] = tmp_ch;
            }
      }
      ret[sz++] = needle;
      ret[sz] = NULL;
      return ret;
}

void narrow_matches(char** cpp, char* needle){
      int ind = 0;
      for(char** i = cpp; *i; ++i){
            if(!strstr(*i, needle)){
                  for(char** j = i; *j; ++j){
                        /* this should implicitly deal with moving over the NULL */
                        *j = j[1];
                  }
                  --i;
            }
            ++ind;
      }
}

char* tab_complete_internal(struct tabcom* tbc, char* base_str, int bs_len, char iter_opts, int* bytes_read, _Bool* free_s){
      _Bool tab;
      char* ret = getline_raw_internal(base_str, bs_len, bytes_read, &tab, NULL), * tmp_ch = NULL;

      *free_s = 1;
      if(tab && tbc){
            _Bool select = 0;
            int maxlen = *bytes_read, tmplen;
            while(!select){
                  for(int tbc_i = 0; tbc_i < tbc->n; ++tbc_i){
                        for(int i = 0; i <= tbc->tbce[tbc_i].optlen; ++i){
                              /* TODO: improve readability */
                              /* select being set to 1 here indicates that we've received a ctrl-c */
                              if(select)break;
                              /* we treat i == optlen of the last index of tbc as the input string */
                              if(i == tbc->tbce[tbc_i].optlen){
                                    /* setting tmp_ch to NULL to indicate that we should skip
                                     * this index if the condition below is not met
                                     */
                                    tmp_ch = NULL;
                                    if(tbc_i == tbc->n-1)tmp_ch = ret;
                              }
                              else{
                                    void* inter = ((char*)tbc->tbce[tbc_i].data_douplep+(i*tbc->tbce[tbc_i].data_blk_sz)+tbc->tbce[tbc_i].data_offset);

                                    /* can't exactly remember this logic -- kinda hard to reason about */
                                    if(tbc->tbce[tbc_i].data_blk_sz == sizeof(char*))tmp_ch = *((char**)inter);
                                    else tmp_ch = (char*)inter;
                                    /* printf("[%i][%i]: (%s, %s)\n", tbc_i, i, ret, tmp_ch); */
                              }
                              #if 0 
                              should we first iterate through all strings and find all matches?
                              this would make it very easy to iterate both backwards and forward
                              the only thing is that it would take a lot of time to compute this initially
                              
                              we could do this original finding of matches in a separate thread!!
                              this would also help to make this program more modular

                              in this case if the user starts iterating while the structure is being built its nbd
                              #endif

                              if(tmp_ch && strstr(tmp_ch, ret)){
                                    /* printing match to screen and removing chars from * old string */
                                    tmplen = (tmp_ch == ret) ? *bytes_read : (int)strlen(tmp_ch);
                                    putchar('\r');
                                    printf("%s", tmp_ch);
                                    if(tmplen > maxlen)maxlen = tmplen;
                                    for(int j = 0; j < maxlen-tmplen; ++j)putchar(' ');
                                    putchar('\r');

                                    /* should we ever exit raw mode during this process? */
                                    raw_mode();

                                    char ch;
                                    while(((ch = getc(stdin)))){
                                          if(ch == 3){
                                                if(*free_s)free(ret);
                                                ret = NULL;
                                                select = 1;
                                                break;
                                          }
                                          if(ch == '\r'){
                                                *bytes_read = tmplen;
                                                if(ret != tmp_ch){
                                                      free(ret);
                                                      /* if we're returning a string that wasn't allocated
                                                       * by us, the user doesn't need to free it
                                                       */
                                                      *free_s = 0;
                                                      ret = tmp_ch;
                                                }
                                                select = 1;
                                                break;
                                          }
                                          if(ch == iter_opts)break;

                                          reset_term();

                                          /* we need to pass along the choice that we're currently on
                                           * before we can recurse, though, we need to append ch to the string
                                           */

                                          /* TODO: handle ch as delete or backspace key */
                                          /* in this case, we'd need to shorten base_str_recurse
                                           */
                                          char base_str_recurse[tmplen+1];
                                          memcpy(base_str_recurse, tmp_ch, tmplen);
                                          base_str_recurse[tmplen] = ch;
                                          if(*free_s)free(ret);

                                          /* this is a pretty nice solution :) */

                                          return tab_complete_internal(tbc, base_str_recurse, tmplen+1, iter_opts, bytes_read, free_s);
                                    }

                                    reset_term();

                                    /* TODO: improve readability */
                                    if(select)break;
                                    continue;
                              }
                              /* TODO: improve readability */
                              /* TODO: is this necessary? */
                              if(select)break;
                        }
                  }
            }
      }
      return ret;
}

_Bool n_char_equiv(char* x, char* y, int n){
      for(int i = 0; i < n; ++i)
            if(!x[i] || !y[i] || x[i] != y[i])return 0;
      return 1;
}

void clear_line(int len, char* str){
      printf("\r%s", str);
      for(int j = 0; j < len; ++j)putchar(' ');
      putchar('\r');
}

char* tab_complete_internal_extra_mem_low_computation(struct tabcom* tbc, char* base_str, int bs_len, char** base_match, char iter_opts[2], int* bytes_read, _Bool* free_s){
      /* TODO: each time we recurse check to see if any chars have been deleted
       * should be easy we can just set a flag because we have to manually handle that
       * anyway
       * if(ch == wtvr)
       * if so, narrow the existing char**
       * implement a function to narrow possibly
       *
       * all narrowing and recreating/rescanning/initial scanning should be done in different threads
       * this will allow 
       *
       * i actually think this is important
       * each time a character is entered we create an initial scan
       * or maybe when strings with >= 2 chars are entered
       *
       * i can even create a complex system where each time a new char is appended we can add a new char** of
       * adjustments to the base char** 
       * each time a char is deleted from current stream we pop off the relevant char**
       * until the base char** which was created from find_matches() has been removed
       */
      _Bool tab;
      char* ret = getline_raw_internal(base_str, bs_len, bytes_read, &tab, NULL), ** tmp_str, ** match = NULL, ** end_ptr = NULL;
      /* ret is only null if ctrl-c */
      *free_s = ret;

      if(tab && tbc){

            {
            _Bool new_search = 1;
            if(base_match){
                  if(base_str && bs_len && n_char_equiv(base_str, ret, bs_len)){
                        match = base_match;
                        new_search = 0;
                  }
            }
            if(new_search){
                  if(base_match)free(base_match);
                  match = find_matches(tbc, ret);
            }
            }

            tmp_str = match;

            int tmplen, maxlen = 0;
            char ch = 0;
            raw_mode();

            while(1){

                  if(!*tmp_str){
                        end_ptr = tmp_str-1;
                        tmp_str = match;
                  }

                  tmplen = strlen(*tmp_str);
                  /* TODO: use prev_len not maxlen */
                  if(maxlen < tmplen)maxlen = tmplen;

                  clear_line(maxlen-tmplen, *tmp_str);

                  ch = getc(stdin);

                  /* selection */
                  if(ch == '\r'){
                        *bytes_read = tmplen;
                        if(ret != *tmp_str){
                              if(ret)free(ret);
                              *free_s = 0;
                              ret = *tmp_str;
                        }
                        break;
                  }

                  if(ch == *iter_opts){
                        ++tmp_str;
                        continue;
                  }
                  if(ch == iter_opts[1]){
                        if(tmp_str != match){
                              --tmp_str;
                              continue;
                        }
                        /* TODO: find_matches should inform us of size of match */
                        /* if we aren't aware of the last index of match */
                        if(!end_ptr){
                              /* tmp_str can't possibly be farther back than match */
                              for(end_ptr = tmp_str; end_ptr[1]; ++end_ptr);
                        }
                        tmp_str = end_ptr;
                        continue;
                  }
                  /* ctrl-c */
                  if(ch == 3){
                        if(*free_s)free(ret);
                        ret = NULL;
                        break;
                  }

                  /* if we've gotten this far it's time to recurse */

                  /* TODO: if ch == delete, recursing should occur with baselen = tmplen
                   * other changes must also be made in this case
                   */

                  /* deletion */
                  _Bool del = ch == 127 || ch == 8;

                  /* +1 for extra char, +1 for \0 */
                  char recurse_str[tmplen+1+((del) ? 0 : 1)];
                  memcpy(recurse_str, *tmp_str, tmplen);
      
                  /*
                   * we could keep an array of match char**s [tmplen, tmplen-1]
                   * each time a deletion is made we go back a step
                   */

                  if(del){
                        clear_line(tmplen, "");
                        recurse_str[--tmplen] = 0;
                        /* delete makes match useless for recurse */
                        free(match);
                        match = NULL;
                  }
                  else{
                        if(!end_ptr)
                              for(end_ptr = tmp_str; end_ptr[1]; ++end_ptr);
                        recurse_str[tmplen++] = ch;
                        recurse_str[tmplen] = 0;
                        /* adjusting the last index of match to user input */
                        *end_ptr = malloc(tmplen);
                        memcpy(*end_ptr, recurse_str, tmplen);

                        narrow_matches(match, recurse_str);
                  }
                  if(*free_s)free(ret);

                  reset_term();
                  /* TODO: URGENT: if del we can generate matches by doing it in a new thread here instead of in the next recurse
                   * this will be a bit tricky to make work between the fucntions though because if getline raw deletes we need to
                   * disqualify this too
                   * we can have a lock that will wait once tabcom internal is entered and this generation isn't yet done
                   * this takes advantage of the time spent in user timescale waiting for input
                   * it's very unlikely that finding matches will take longer than user input
                   * even if user deletes chars in getline, we're no worse off
                   * high memory usage but should be very fast
                   */
                  return tab_complete_internal_extra_mem_low_computation(tbc, recurse_str, tmplen, match, iter_opts, bytes_read, free_s);

            }

            reset_term();

            /*if(base_match && *end_ptr)free(*end_ptr);*/
            free(match);
      }
      return ret;
}

/* tab_complete behaves like getline(), but does not include \n char in returned string */
/* *free_s is set to 1 if returned buffer should be freed */
char* tab_complete(struct tabcom* tbc, char iter_opts[2], int* bytes_read, _Bool* free_s){
      #if LOW_MEM
      return tab_complete_internal(tbc, NULL, 0, *iter_opts, bytes_read, free_s);
      #else
      return tab_complete_internal_extra_mem_low_computation(tbc, NULL, 0, NULL, iter_opts, bytes_read, free_s);
      #endif
}
