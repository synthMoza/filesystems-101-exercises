package parhash

import (
	"context"
	"log"
	"net"
	"sync"

	"github.com/pkg/errors"
	"github.com/prometheus/client_golang/prometheus"
	"golang.org/x/sync/semaphore"
	"google.golang.org/grpc"

	"fs101ex/pkg/workgroup"

	hashpb "fs101ex/pkg/gen/hashsvc"
	parhashpb "fs101ex/pkg/gen/parhashsvc"
)

type Config struct {
	ListenAddr   string
	BackendAddrs []string
	Concurrency  int

	Prom prometheus.Registerer
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
//
// The server must report the following performance counters to Prometheus:
//
//  1. nr_nr_requests: a counter that is incremented every time a call
//     is made to ParallelHash(),
//
//  2. subquery_durations: a histogram that tracks durations of calls
//     to backends.
//     It must have a label `backend`.
//     Each subquery_durations{backed=backend_addr} must be a histogram
//     with 24 exponentially growing buckets ranging from 0.1ms to 10s.
//
// Both performance counters must be placed to Prometheus namespace "parhash".
type Server struct {
	conf Config

	stop            context.CancelFunc
	l               net.Listener
	wg              sync.WaitGroup
	mutex           sync.Mutex // sync between concurrent ParallelHash() calls
	globalBufferIdx int        // counter for ignoring order of goroutines
	
	callCounter     prometheus.Counter
	backendHist		*prometheus.HistogramVec

	sem *semaphore.Weighted
}

func New(conf Config) *Server {
	return &Server{
		conf:            conf,
		sem:             semaphore.NewWeighted(int64(conf.Concurrency)),
		globalBufferIdx: 0, // default init
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

	// intiliaze ParallelHash() call counter
	s.callCounter = prometheus.NewCounter(
		prometheus.CounterOpts{
			Namespace: "parhash",
			Name: "nr_requests",
		})
	s.conf.Prom.MustRegister(s.callCounter)

	s.backendHist = prometheus.NewHistogramVec(prometheus.HistogramOpts{
		Namespace: "parhash",
		Name: "subquery_durations",
		Buckets: prometheus.ExponentialBucketsRange(0.1, 10000, 24), // milliseconds
		},
		[]string{"backend"},
	)
	s.conf.Prom.MustRegister(s.backendHist)

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
	// record call metric
	s.callCounter.Inc()

	countBackends := len(s.conf.BackendAddrs)
	connectionsSlice := make([]*grpc.ClientConn, countBackends)
	clientsSlice := make([]hashpb.HashSvcClient, countBackends)

	// connect to backends
	for i := 0; i < countBackends; i++ {
		connectionsSlice[i], err = grpc.Dial(s.conf.BackendAddrs[i], grpc.WithInsecure())
		if err != nil {
			log.Fatalf("Couldn't connect to backend addr %s with error %s", s.conf.BackendAddrs[i], err)
		}

		defer connectionsSlice[i].Close()

		clientsSlice[i] = hashpb.NewHashSvcClient(connectionsSlice[i])
	}

	countBuffers := len(req.Data)
	hashes := make([][]byte, countBuffers)

	// limit goroutines count with semaphore
	waitGroup := workgroup.New(workgroup.Config{Sem: s.sem})
	for i := 0; i < countBuffers; i++ {
		currentBufferIdx := i
		waitGroup.Go(ctx, func(ctx context.Context) error {
			s.mutex.Lock()
			// claim indices under lock
			currentBackendIdx := s.globalBufferIdx
			// update global idx in round robin manner
			s.globalBufferIdx = (s.globalBufferIdx + 1) % countBackends

			hashReq := hashpb.HashReq{Data: req.Data[currentBufferIdx]}
			
			s.mutex.Unlock()
			
			// measure time that was taken to request backends
			timer := prometheus.NewTimer(prometheus.ObserverFunc(func(v float64) {
				s.backendHist.WithLabelValues(s.conf.BackendAddrs[currentBackendIdx]).Observe(v)
			}))

			r, err := clientsSlice[currentBackendIdx].Hash(ctx, &hashReq)
			
			timer.ObserveDuration()
			if err != nil {
				return err
			}

			s.mutex.Lock()
			hashes[currentBufferIdx] = r.Hash
			s.mutex.Unlock()

			return nil
		})
	}

	err = waitGroup.Wait()
	if err != nil {
		log.Fatalf("Error: can't hash given data with error %v", err)
	}

	return &parhashpb.ParHashResp{Hashes: hashes}, nil
}
