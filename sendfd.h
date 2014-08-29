#ifndef _SENDFD_H_
#define _SENDFD_H_

int sendfd(int s, int fd);
int recvfd(int s);
int recv_fds(int sock, int *fds);
int send_fds(int sock, const int *fds);

#endif /* _SENDFD_H_ */
