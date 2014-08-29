#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include "atomic.h"
#include "mempool.h"
#include "sendfd.h"

int sendfd(int sock, int fd)
{
	struct msghdr msghdr;
	char nothing;
	struct iovec nothing_ptr;
	struct cmsghdr *cmsg;
	struct {
		struct cmsghdr h;
		int fd;
	} buffer;

	nothing_ptr.iov_base = &nothing;
	nothing_ptr.iov_len = 1;
	msghdr.msg_name = NULL;
	msghdr.msg_namelen = 0;
	msghdr.msg_iov = &nothing_ptr;
	msghdr.msg_iovlen = 1;
	msghdr.msg_flags = 0;
	msghdr.msg_control = &buffer;
	msghdr.msg_controllen = sizeof(struct cmsghdr) + sizeof(int);
	cmsg = CMSG_FIRSTHDR(&msghdr);
	cmsg->cmsg_len = msghdr.msg_controllen;
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_RIGHTS;
	memmove(CMSG_DATA(cmsg), &fd, sizeof(int));

	return sendmsg(sock, &msghdr, 0) < 0 ? -1: 0;
}

int recvfd(int sock)
{
	struct msghdr msghdr;
	char nothing;
	struct iovec nothing_ptr;
	struct cmsghdr *cmsg;
	struct {
		struct cmsghdr h;
		int fd;
	} buffer;
	int fd;

	nothing_ptr.iov_base = &nothing;
	nothing_ptr.iov_len = 1;
	msghdr.msg_name = NULL;
	msghdr.msg_namelen = 0;
	msghdr.msg_iov = &nothing_ptr;
	msghdr.msg_iovlen = 1;
	msghdr.msg_flags = 0;
	msghdr.msg_control = &buffer;
	msghdr.msg_controllen = sizeof(struct cmsghdr) + sizeof(int);
	cmsg = CMSG_FIRSTHDR(&msghdr);
	cmsg->cmsg_len = msghdr.msg_controllen;
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_RIGHTS;

	if (recvmsg(sock, &msghdr, 0) <= 0)
		return -1;

	memmove(&fd, (int *)CMSG_DATA(cmsg), sizeof(int));

	return fd;
}
