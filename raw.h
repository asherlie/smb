void raw_mode();
void reset_term();
char* getline_raw(int* bytes_read, _Bool* tab, int* ignore);
char* tab_complete(void* data_douplep, int data_blk_sz, int data_offset, int optlen, char iter_opts, int* bytes_read);
