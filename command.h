#ifndef _COMMAND_H_
#define _COMMAND_H_

typedef enum fd_cmd_t {
	GET_FDS,
	QUIT,
} fd_cmd_t;

int mp_send_fds(mempool_priv_t *mp_priv, unsigned clients);
int mp_recv_fds(mempool_priv_t *mp_priv);
int mp_cmd_wait_fork(mempool_priv_t *mp_priv);
int mp_send_cmd(mempool_priv_t *mp_priv, fd_cmd_t cmd);

#endif /* _COMMAND_H_ */
