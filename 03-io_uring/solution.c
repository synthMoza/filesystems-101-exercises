#include <solution.h>

#include <liburing.h>
#include <assert.h>
#include <stdlib.h>

#define ENTITIES 8

#define N 4 // number of intial read requests
#define REQ_SIZE (256 * 1024) // 256KB in bytes

#define CHECK_RETURN(ret)					\
if ((ret) < 0)								\
	return ret;								\

#define CHECK_RETURN_ERRNO(ret)				\
if ((ret) < 0)								\
	return -errno;							\

struct io_data {
    int read;
    off_t first_offset, offset;
    size_t first_len;
    struct iovec iov;
};

static void queue_prepped(int in, int out, struct io_uring *ring, struct io_data *data)
{
	struct io_uring_sqe *sqe;

	sqe = io_uring_get_sqe(ring);
	assert(sqe);

	if (data->read)
		io_uring_prep_readv(sqe, in, &data->iov, 1, data->offset);
	else
		io_uring_prep_writev(sqe, out, &data->iov, 1, data->offset);

	io_uring_sqe_set_data(sqe, data);
}

static int queue_read(int in, struct io_uring *ring, off_t size, off_t offset) {
    struct io_uring_sqe *sqe;
    struct io_data *data;

    data = malloc(size + sizeof(*data));
    if (!data)
        return 1;

    sqe = io_uring_get_sqe(ring);
    if (!sqe) {
        free(data);
        return 1;
    }

    data->read = 1;
    data->offset = data->first_offset = offset;

    data->iov.iov_base = data + 1;
    data->iov.iov_len = size;
    data->first_len = size;

    io_uring_prep_readv(sqe, in, &data->iov, 1, offset);
    io_uring_sqe_set_data(sqe, data);
    return 0;
}

static void queue_write(int in, int out, struct io_uring *ring, struct io_data *data) {
    data->read = 0;
    data->offset = data->first_offset;

    data->iov.iov_base = data + 1;
    data->iov.iov_len = data->first_len;

    queue_prepped(in, out, ring, data);
    io_uring_submit(ring);
}

static int setup_context(unsigned int entries, struct io_uring* ring)
{
	if (io_uring_queue_init(entries, ring, 0) < 0)
		return -1;

	return 0;
}

static void destroy_context(struct io_uring* ring)
{
	io_uring_queue_exit(ring);
}

static int get_file_size(int fd, off_t* size)
{
	assert(size);

	int ret = 0;
	struct stat st;

	ret = fstat(fd, &st); 
	CHECK_RETURN(ret);
	
	if (S_ISREG(st.st_mode))
	{
		*size = st.st_size;
		return 0;
	}
	else
	{
		return -1; // guaranteed regular files
	}
}

static int copy_impl(int in, int out, struct io_uring* ring)
{
	int ret;
	unsigned long reads, writes;
    struct io_uring_cqe *cqe;
    off_t write_left, offset;

	off_t insize = 0;
	
	ret = get_file_size(in, &insize);
	CHECK_RETURN(ret);

    write_left = insize;
    writes = reads = offset = 0;

	// queue N read requests
	for (size_t i = 0; i < N; ++i)
	{
		off_t this_size = insize;

		if (reads + writes >= ENTITIES)
			break;
		if (this_size > REQ_SIZE)
			this_size = REQ_SIZE;
		else if (!this_size)
			break;

		if (queue_read(in, ring, this_size, offset))
			break;

		insize -= this_size;
		offset += this_size;
		reads++;
	}

	ret = io_uring_submit(ring);
	CHECK_RETURN(ret);

	int to_read = 0;
	while (insize || write_left) {
		if (to_read > 0)
		{
			off_t this_size = insize;

			if (reads + writes >= ENTITIES)
				break;
			if (this_size > REQ_SIZE)
				this_size = REQ_SIZE;
			else if (!this_size)
				break;

			if (queue_read(in, ring, this_size, offset))
				break;

			insize -= this_size;
			offset += this_size;
			reads++;

			ret = io_uring_submit(ring);
			CHECK_RETURN(ret);

			to_read = 0;
		}

        int got_comp;

        got_comp = 0;
        while (write_left)
		{
            struct io_data *data;

            if (!got_comp)
			{
                ret = io_uring_wait_cqe(ring, &cqe);
                got_comp = 1;
            }
			else
			{
                ret = io_uring_peek_cqe(ring, &cqe);
                if (ret == -EAGAIN)
				{
                    cqe = NULL;
                    ret = 0;
                }
            }

            CHECK_RETURN_ERRNO(ret);
            
			if (!cqe)
                break;

            data = io_uring_cqe_get_data(cqe);
            if (cqe->res < 0) 
			{
                if (cqe->res == -EAGAIN) {
                    queue_prepped(in, out, ring, data);
					io_uring_submit(ring);
					io_uring_cqe_seen(ring, cqe);
                    continue;
                }

                return cqe->res; // -errno
            } else if (cqe->res != (signed int) data->iov.iov_len) {
                // short read/write; adjust and requeue
                data->iov.iov_base += cqe->res;
                data->iov.iov_len -= cqe->res;
                queue_prepped(in, out, ring, data);
                io_uring_cqe_seen(ring, cqe);
                continue;
            }

            if (data->read) 
			{
				// when any of read request is done, create write and read requests
                queue_write(in, out, ring, data);
                write_left -= data->first_len;
                reads--;
                writes++;

				// queue another read, if needed
				to_read = 1;
			} 
			else 
			{
				free(data);
				writes--;
			}

			io_uring_cqe_seen(ring, cqe);
        }
    }

	while (writes) {
		struct io_data *data;

		ret = io_uring_wait_cqe(ring, &cqe);
		CHECK_RETURN(ret);

		CHECK_RETURN(cqe->res);

		data = io_uring_cqe_get_data(cqe);
		free(data);
		writes--;
		io_uring_cqe_seen(ring, cqe);
	}

	return 0;
}

int copy(int in, int out)
{
	struct io_uring ring;
	int ret = 0;
	
	ret = setup_context(ENTITIES, &ring);
	CHECK_RETURN(ret);
	
	ret = copy_impl(in, out, &ring);
	CHECK_RETURN(ret);

	destroy_context(&ring);
	return 0;
}
