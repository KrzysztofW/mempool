#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "sendfd.h"
#include "mempool.h"
#include "command.h"

static int __recv_fds(mempool_priv_t *mp_priv, int sock)
{
	int count = 0, i;

	for (i = 0; i < MEM_POOL_MAX_FDS; i++) {
		int fd;

		if ((fd = recvfd(sock)) < 0) {
			close(sock);
			return count;
		}
		count++;
		mp_priv->fds[i] = fd;
	}

	return count;
}

int mp_recv_fds(mempool_priv_t *mp_priv)
{
	int sock, count = 0;
	struct sockaddr_un client;

	sock = socket(AF_UNIX, SOCK_STREAM, 0);
	if (sock < 0) {
		perror("opening stream socket");
		return -1;
	}
	memset(&client, 0, sizeof(client));
	client.sun_family = AF_UNIX;
	strncpy(client.sun_path, mp_priv->mp->sun_path,
		sizeof(client.sun_path - 1));

	if (connect(sock, (struct sockaddr *) &client, sizeof(client)) < 0) {
		close(sock);
		perror("connecting stream socket");
		return -1;
	}
	count = __recv_fds(mp_priv, sock);
	close(sock);

	return count;
}

static void set_sun_path(struct sockaddr_un *sock, mempool_priv_t *mp_priv)
{
	strncpy(mp_priv->mp->sun_path, "/tmp/",
		sizeof(mp_priv->mp->sun_path) - 1);

	strncpy(mp_priv->mp->sun_path + 5, mp_priv->mp->name,
		sizeof(mp_priv->mp->sun_path) - 6);

	strncpy(sock->sun_path, mp_priv->mp->sun_path,
		sizeof(sock->sun_path) - 1);

	strncpy(sock->sun_path, mp_priv->mp->sun_path,
		sizeof(sock->sun_path - 1));
}

int mp_socket_bind(mempool_priv_t *mp_priv)
{
	int sock;
	struct sockaddr_un server;

	sock = socket(AF_UNIX, SOCK_STREAM, 0);
	if (sock < 0) {
		perror("opening stream socket");
		return -1;
	}

	memset(&server, 0, sizeof(server));
	server.sun_family = AF_UNIX;
	set_sun_path(&server, mp_priv);

	if (access(server.sun_path, F_OK) == 0) {
		if (access(server.sun_path, R_OK | W_OK) < 0) {
			fprintf(stderr, "%s: access denied\n", server.sun_path);
			return -1;
		}
		/* remove the socket if already exists */
		unlink(server.sun_path);
	}

	if (bind(sock, (struct sockaddr *) &server, sizeof(server)) < 0) {
		perror("binding stream socket");
		return -1;
	}
	listen(sock, 5);

	return sock;
}

static int __send_fds(mempool_priv_t *mp_priv, int sock)
{
	int i, count = 0;

	for (i = 0; i < MEM_POOL_MAX_FDS; i++) {
		int fd = mp_priv->fds[i];

		if (fd < 0)
			return count;

		if (sendfd(sock, mp_priv->fds[i]) < 0)
			return -1;

		count++;
	}

	return count;
}

int mp_send_cmd(mempool_priv_t *mp_priv, fd_cmd_t cmd)
{
	int sock;
	struct sockaddr_un client;

	sock = socket(AF_UNIX, SOCK_STREAM, 0);
	if (sock < 0) {
		perror("opening stream socket");

		return -1;
	}
	memset(&client, 0, sizeof(client));
	client.sun_family = AF_UNIX;
	set_sun_path(&client, mp_priv);

	if (connect(sock, (struct sockaddr *)&client, sizeof(client)) < 0) {
		close(sock);
		perror("connecting stream socket");
		return -1;
	}

	if (write(sock, &cmd, sizeof(fd_cmd_t)) < 0) {
		fprintf(stderr, "failed to write cmd %d\n", cmd);
		return -1;
	}

	switch (cmd) {
	case SEND_FDS:
		__recv_fds(mp_priv, sock);
		break;

	case QUIT:
		break;
	}

	close(sock);

	return 0;
}

static char *sun_path;

static void cleanup_sun(int signo)
{
	if (sun_path)
		unlink(sun_path);
}

int mp_cmd_wait_fork(mempool_priv_t *mp_priv)
{
	int pid;
	int sock;

	pid = fork();
	if (pid < 0) {
		fprintf(stderr, "fork has failed\n");
		return -1;
	}

	if (pid != 0) {
		signal(SIGCHLD, SIG_IGN);
		return 0;
	}

	sock = mp_socket_bind(mp_priv);
	if (sock < 0)
		return -1;
	sun_path = mp_priv->mp->sun_path;

	if (signal(SIGINT, cleanup_sun) == SIG_ERR) {
		fprintf(stderr, "can't catch SIGINT\n");
		return -1;
	}

	while (1) {
		int msgsock;
		fd_cmd_t cmd;

		msgsock = accept(sock, 0, 0);
		if (msgsock == -1) {
			printf("sun_path=%s\n", sun_path);
			perror("accept");
			return -1;
		}
		if (read(msgsock, &cmd, sizeof(fd_cmd_t)) < 0) {
			fprintf(stderr, "failed receiving command\n");
			close(msgsock);
			continue;
		}

		switch (cmd) {
		case SEND_FDS:
			if (__send_fds(mp_priv, msgsock) < 0)
				fprintf(stderr,
					"failed sending file descriptors\n");
			break;

		case QUIT:
			fprintf(stdout, "exiting command daemon\n");
			close(msgsock);
			close(sock);
			unlink(sun_path);
			exit(0);

		default:
			fprintf(stderr, "unknown command %d\n", cmd);
		}
		close(msgsock);
	}

	close(sock);
	unlink(sun_path);

	return 0;
}

int mp_send_fds(mempool_priv_t *mp_priv, unsigned clients)
{
	int ret = 0, msgsock, sock = mp_socket_bind(mp_priv);
	char *sun_path = mp_priv->mp->sun_path;

	if (sock < 0)
		return -1;

	do {
		int i;

		msgsock = accept(sock, 0, 0);
		if (msgsock == -1) {
			perror("accept");
			continue;
		}

		for (i = 0; i < MEM_POOL_MAX_FDS; i++) {
			int fd = mp_priv->fds[i];

			if (fd < 0) {
				ret = 0;
				close(msgsock);
				goto end;
			}

			if (sendfd(msgsock, mp_priv->fds[i]) < 0) {
				ret = -1;
				close(msgsock);
				goto end;
			}
		}
		close(msgsock);
	} while (clients--);

 end:
	close(sock);
	unlink(sun_path);

	return ret;
}
