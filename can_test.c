/*
* *  This code is licensed under the MIT License.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <linux/can.h>
#include <linux/can/raw.h>

#define MAX_SEND_BUF		(64)
#define MAX_RCV_BUF			(64)
/**defination of can frame pool*/
struct can_frame_buf
{
	struct can_frame frame;
	int flag;
};

/**can send pool*/
struct can_frame_buf can_send_buf[MAX_SEND_BUF];
/**can receiver pool*/
struct can_frame_buf can_rcv_buf[MAX_RCV_BUF];

/**-------------------------------------------------------
 * @brief can socket init funtion
 * @param name  name of can interface
 * 
 * @return int 
 *  			< 0 ---- creat failed
 *  			> 0 ---- creat socket success
 *-------------------------------------------------------*/
int can_send_init(const char *name)
{
	const char *ifname = name;
	struct sockaddr_can addr;
	struct ifreq ifr;
	int can_sock_fd = -1;

	if((can_sock_fd = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0) {
		printf("socket error: %s, errno=%d.\n", strerror(errno), errno);
		return (-1);
	}
	strcpy(ifr.ifr_name, ifname);
	ioctl(can_sock_fd, SIOCGIFINDEX, &ifr);
	addr.can_family = AF_CAN;
	addr.can_ifindex = ifr.ifr_ifindex;

	if(bind(can_sock_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		printf("bind error: %s, errno=%d.\n", strerror(errno), errno);
		return (-2);
	}
	printf("creat %s fd = %d \n", ifname, can_sock_fd);
	return (can_sock_fd);
}

/**-------------------------------------------------------
 * @brief func that can frame package and put it to pool.
 * @param can_id - can id of will be send
 * @param data  -  can frame data field
 * @param len  - data length of can frame data.
 * 
 * @return int 
 *  			< 0 failed
 *  			=0  success
 *-------------------------------------------------------*/
int can_send(canid_t can_id, unsigned char *data, int len)
{
	int i, j;

	if (len < 0) return (-1);

	for (j = 0; j < MAX_SEND_BUF; j++) {
		if (can_send_buf[j].flag != 1) {
			break;
		}
	}

	if (j >= MAX_SEND_BUF) {
		printf("send1 canbus err\n");
		return (-1);
	}
	can_send_buf[j].frame.can_id = can_id;
	can_send_buf[j].frame.can_dlc = len;
	memset(can_send_buf[j].frame.data, 0x00, 8);
	for (i = 0; i < len; i++) {
		can_send_buf[j].frame.data[i] = data[i];
	}
	can_send_buf[j].flag = 1;
	printf("send:[%x] %02x %02x %02x %02x %02x %02x %02x %02x\n", can_send_buf[j].frame.can_id,
		   can_send_buf[j].frame.data[0], can_send_buf[j].frame.data[1], can_send_buf[j].frame.data[2],
		   can_send_buf[j].frame.data[3], can_send_buf[j].frame.data[4], can_send_buf[j].frame.data[5],
		   can_send_buf[j].frame.data[6], can_send_buf[j].frame.data[7]);
	return (0);
}

/*!-------------------------------------------------------
 * \brief thread of can send
 * \param arg thread's parameter
 * 
 * \return void* 
 -------------------------------------------------------*/
void *can_send_thread(void *arg)
{
	int i, nbytes;
	int can_sock_fd = -1;

	while (1)
	{
		if ((can_sock_fd = can_send_init("can0")) < 0)
		{
			printf("can0 send socket init error!\n");
			sleep(1);
			continue;
		}
		
		while (1) {
			for (i = 0; i < MAX_SEND_BUF; i++) {
				if (can_send_buf[i].flag == 1) {
					if ((nbytes = write(can_sock_fd, &(can_send_buf[i].frame), sizeof(struct can_frame))) <= 0) {
						printf("write error: %s, errno=%d.\n", strerror(errno), errno);
						close(can_sock_fd);
						can_sock_fd = -1;
						break;
					}
					can_send_buf[i].flag = 0;
				}
			}
			if (can_sock_fd < 0)
			{
				break;
			}
			usleep(3000);
		}
	}
	return (NULL);
}

/*!-------------------------------------------------------
 * \brief put can to pool
 * \param frame frame that put in
 -------------------------------------------------------*/
void _insert_can0_data(struct can_frame frame)
{
	int i;

	for (i = 0; i < MAX_RCV_BUF; i++)
	{
		if (can_rcv_buf[i].flag != 1)
		{
			break;
		}
	}
	if (i >= MAX_RCV_BUF)
	{
		printf("rcv canbus full\n");
	}

	memset((char *)&can_rcv_buf[i].frame, 0x00, sizeof(can_rcv_buf[i].frame));
	can_rcv_buf[i].frame = frame;
	can_rcv_buf[i].flag = 1;
}
/*!-------------------------------------------------------
 * \brief handler of handling frame from receiver thread
 -------------------------------------------------------*/
void process_can0_msg()
{
	int i = 0;

	for (i = 0; i < MAX_RCV_BUF; i++)
	{
		if (can_rcv_buf[i].flag == 1)
		{
			printf("recv:[%x] %02x %02x %02x %02x %02x %02x %02x %02x\n", can_rcv_buf[i].frame.can_id,
				   can_rcv_buf[i].frame.data[0], can_rcv_buf[i].frame.data[1], can_rcv_buf[i].frame.data[2],
				   can_rcv_buf[i].frame.data[3], can_rcv_buf[i].frame.data[4], can_rcv_buf[i].frame.data[5],
				   can_rcv_buf[i].frame.data[6], can_rcv_buf[i].frame.data[7]);
			can_rcv_buf[i].flag = 0;
		}
	}
}
/*!-------------------------------------------------------
 * \brief thread to handle frame
 * \param arg 
 * 
 * \return void* 
 -------------------------------------------------------*/
void *can_recv_thread(void *arg)
{
	struct timeval wait;
	int can_sock_fd = -1;

	int n_ready;
	fd_set readfds;
	struct can_frame frame;
	struct can_filter rfilter[8];/*can filter*/

	while (1)
	{
		if ((can_sock_fd = can_send_init("can0")) < 0)
		{
			printf("can0 recv socket init error!\n");
			sleep(1);
			continue;
		}
		printf("can0 recv thread ready...\n");
		while(1) {
			wait.tv_sec = 0;
			wait.tv_usec = 5000;

			FD_ZERO(&readfds);
			FD_SET(can_sock_fd, &readfds);

			n_ready = select(can_sock_fd + 1, &readfds, NULL, NULL, &wait);
			if (n_ready) {
				if(FD_ISSET(can_sock_fd, &readfds)) {
					if (read(can_sock_fd, &frame, sizeof(frame)) > 0) {
						_insert_can0_data(frame);
					}
					else {
						close(can_sock_fd);
						break;
					}
				}
			}
		}
	}
	return (NULL);
}

int init_can_thread(void)
{
	pthread_t thread_t;

	if (pthread_create(&thread_t, NULL, can_recv_thread, NULL) != 0)
	{
		return (-1);
	}
	if (pthread_create(&thread_t, NULL, can_send_thread, NULL) != 0)
	{
		return (-2);
	}

	return (0);
}

int main(int argc, char *args[])
{
	init_can_thread();
	canid_t can_id = 0x1234;
	unsigned int  frame_order = 0;
	unsigned char data[8] = { 0 };
	while (1)
	{
		frame_order++;/*plus order every time*/
		memcpy(data, (unsigned char *)&frame_order, sizeof(frame_order));
		can_send(can_id, data, sizeof(data));
		process_can0_msg();
		usleep(10*1000);/*force to wait*/
	}
	return (1);
}
