#ifndef INC_31_SOCKET_OPERATIONS_H
#define INC_31_SOCKET_OPERATIONS_H

#endif //INC_31_SOCKET_OPERATIONS_H

//TODO:
//  1. Make socket operations library
//      containing send, receive and hostname_to_ip functions
//  2. Make http client

int buffer_to_string(char* buffer, size_t size, char** string);

int hostname_to_ip(char* hostname, size_t hostname_len, char* ip);

ssize_t write_all(int sock, const char* buf, size_t buf_len);

ssize_t read_all(int sock, char* buf, size_t buf_len);
