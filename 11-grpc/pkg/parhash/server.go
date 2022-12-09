package parhash

import (
	"context"
	"log"
	"net"
	"sync"

	"github.com/pkg/errors"
	"golang.org/x/sync/semaphore"
	"google.golang.org/grpc"

	hashpb "fs101ex/pkg/gen/hashsvc"
	parhashpb "fs101ex/pkg/gen/parhashsvc"
)

type Config struct {
	ListenAddr   string
	BackendAddrs []string
	Concurrency  int
}

// Implement a server that responds to ParallelHash()
// as declared in /proto/parhash.proto.
//
// The implementation of ParallelHash() must not hash the content
// of buffers on its own. Instead, it must send buffers to backends
// to compute hashes. Buffers must be fanned out to backends in the
// round-robin fashion.
//
// For example, suppose that 2 backends are configured and ParallelHash()
// is called to compute hashes of 5 buffers. In this case it may assign
// buffers to backends in this way:
//
//	backend 0: buffers 0, 2, and 4,
//	backend 1: buffers 1 and 3.
//
// Requests to hash individual buffers must be issued concurrently.
// Goroutines that issue them must run within /pkg/workgroup/Wg. The
// concurrency within workgroups must be limited by Server.sem.
//
// WARNING: requests to ParallelHash() may be concurrent, too.
// Make sure that the round-robin fanout works in that case too,
// and evenly distributes the load across backends.
type Server struct {
	conf Config

	stop  context.CancelFunc
	l     net.Listener
	wg    sync.WaitGroup
	mutex sync.Mutex // sync between concurrent ParallelHash() calls

	sem *semaphore.Weighted
}

func New(conf Config) *Server {
	return &Server{
		conf: conf,
		sem:  semaphore.NewWeighted(int64(conf.Concurrency)),
	}
}

func (s *Server) Start(ctx context.Context) (err error) {
	defer func() { err = errors.Wrap(err, "Start()") }()

	// ctx with cancel allows to cancel all goroutines in case of error
	ctx, s.stop = context.WithCancel(ctx)

	s.l, err = net.Listen("tcp", s.conf.ListenAddr)
	if err != nil {
		return err
	}

	srv := grpc.NewServer()
	parhashpb.RegisterParallelHashSvcServer(srv, s)

	s.wg.Add(2)
	go func() {
		defer s.wg.Done()

		srv.Serve(s.l)
	}()
	go func() {
		defer s.wg.Done()

		<-ctx.Done()
		s.l.Close()
	}()

	return nil
}

func (s *Server) ListenAddr() string {
	return s.l.Addr().String()
}

func (s *Server) Stop() {
	s.stop()
	s.wg.Wait()
}

func (s *Server) ParallelHash(ctx context.Context, req *parhashpb.ParHashReq) (resp *parhashpb.ParHashResp, err error) {
	// calls to ParallelHash might be concurrent, serve each responce once at a time
	s.mutex.Lock()

	// connect to backends
	countBackends := s.conf.Concurrency
	connectionsSlice := make([]*grpc.ClientConn, s.conf.Concurrency)
	clientsSlice := make([]hashpb.HashSvcClient, s.conf.Concurrency)

	for i := 0; i < countBackends; i++ {
		connectionsSlice[i], err = grpc.Dial(s.conf.BackendAddrs[i])
		if err != nil {
			log.Fatalf("Couldn't connect to backend addr %s", s.conf.BackendAddrs[i])
		}

		defer connectionsSlice[i].Close()

		clientsSlice[i] = hashpb.NewHashSvcClient(connectionsSlice[i])
	}

	// distribute data across given amount of backends
	// buffer[i] belongs to i % countBackends backend
	buffersCount := len(req.Data)

	// create countBackends goroutines that perform requests and then write to the responce
	hashes := make([][]byte, countBackends)
	defer s.stop()

	for i := 0; i < countBackends; i++ {
		go func() {
			for j := i; j < buffersCount; j += countBackends {
				// send request and get responce
				r, err := clientsSlice[i].Hash(ctx, &hashpb.HashReq{Data: req.Data[i]})
				if err != nil {
					log.Fatalf("Couldn't hash buffer number %d on backend %d with error %v", err)
				}

				// write into responce using blocking with semaphore
				s.sem.Acquire(ctx, 1)
				hashes[j] = r.Hash
				s.sem.Release(1)
			}
		}()
	}

	s.mutex.Unlock()
	return &parhashpb.ParHashResp{Hashes: hashes}, nil
}
