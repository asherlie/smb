#include <sys/socket.h>
#include <stdlib.h>

#include "shared.h"

int listen_sock(){
      int sock = socket(AF_UNIX, SOCK_STREAM, 0);
      return sock;
}

_Bool strtoi(const char* str, int* i){
      char* res;
      int r = strtol(str, &res, 10);
      if(*res)return 0;
      *i = (int)r;
      return 1;
}

