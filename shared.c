#include <sys/socket.h>

#include "shared.h"

int listen_sock(){
      int sock = socket(AF_UNIX, SOCK_STREAM, 0);
      return sock;
}


