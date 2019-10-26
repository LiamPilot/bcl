#ifndef BCL_ARH_AM_AGGREGATE_HPP
#define BCL_ARH_AM_AGGREGATE_HPP

#include <vector>
#include "arh_am.hpp"
#include "arh_agg_buffer.hpp"
#ifdef ARH_PROFILE
#include "arh_tools.hpp"
#endif

namespace ARH {
#ifdef ARH_PROFILE
  double ticks_load = 0; // rpc_agg lock-unlock without send
  double ticks_agg_buf_npop = 0; // rpc_agg lock-unlock without send
  double ticks_agg_buf_pop = 0; // rpc_agg lock-unlock with send
  double ticks_gex_req = 0; // rpc_agg lock-unlock with send
#endif
  std::vector<AggBuffer<rpc_t>> agg_buffers;
  size_t max_agg_size;
  std::atomic<size_t> agg_size;

  void init_agg() {
    max_agg_size = MIN(
        gex_AM_MaxRequestMedium(BCL::tm,GEX_RANK_INVALID,GEX_EVENT_NOW,0,0) / sizeof(rpc_t),
        gex_AM_MaxReplyMedium  (BCL::tm,GEX_RANK_INVALID,GEX_EVENT_NOW,0,0) / sizeof(rpc_result_t)
        );
    agg_size = max_agg_size;

    agg_buffers = std::vector<AggBuffer<rpc_t>>(nprocs());
    for (size_t i = 0; i < nprocs(); ++i) {
      agg_buffers[i].init(agg_size);
    }
  }

  size_t set_agg_size(size_t custom_agg_size) {
#ifdef ARH_DEBUG
    assert(custom_agg_size > 0);
#endif
    agg_size = MIN(agg_size.load(), custom_agg_size);
    for (size_t i = 0; i < nprocs(); ++i) {
      agg_buffers[i].init(agg_size);
    }
    return agg_size.load();
  }

  size_t get_max_agg_size() {
    return max_agg_size;
  }

  size_t get_agg_size() {
    return agg_size.load();
  }

  void flush_agg_buffer() {
    for (size_t i = my_worker_local(); i < nprocs(); i += nworkers_local()) {
      std::vector<rpc_t> send_buf;
      agg_buffers[i].pop_nofull(send_buf);
      if (!send_buf.empty()) {
        requested += send_buf.size();
        generic_handler_request_impl_(i, std::move(send_buf));
      }
    }
  }

  template <typename Fn, typename... Args>
  Future<std::invoke_result_t<Fn, Args...>>
  rpc_agg(size_t remote_worker, Fn&& fn, Args&&... args) {
    assert(remote_worker < nworkers());

    size_t remote_proc = remote_worker / nworkers_local();
    u_int8_t remote_worker_local = (u_int8_t) remote_worker % nworkers_local();

#ifdef ARH_PROFILE
    tick_t start_load = ticks_now();
#endif
    Future<std::invoke_result_t<Fn, Args...>> future;
    rpc_t my_rpc(future.get_p(), remote_worker_local);
    my_rpc.load(std::forward<Fn>(fn), std::forward<Args>(args)...);
#ifdef ARH_PROFILE
    static int step_load = 0;
    tick_t end_load = ticks_now();
    if (my_worker_local() == 0) {
      update_average(ticks_load, end_load - start_load, ++step_load);
    }
#endif

#ifdef ARH_PROFILE
    tick_t start = ticks_now();
#endif
    auto status = agg_buffers[remote_proc].push(std::move(my_rpc));
    while (status == AggBuffer<rpc_t>::status_t::FAIL) {
      progress();
      status = agg_buffers[remote_proc].push(std::move(my_rpc));
    }
    if (status == AggBuffer<rpc_t>::status_t::SUCCESS_AND_FULL) {
      std::vector<rpc_t> send_buf;
      agg_buffers[remote_proc].pop_full(send_buf);
      requested += send_buf.size();
#ifdef ARH_PROFILE
      static int step_agg_buffer_pop = 0;
      tick_t end = ticks_now();
      if (my_worker_local() == 0) {
        update_average(ticks_agg_buf_pop, end - start, ++step_agg_buffer_pop);
      }
#endif

#ifdef ARH_PROFILE
      tick_t start_gex_req = ticks_now();
#endif
      generic_handler_request_impl_(remote_proc, std::move(send_buf));
#ifdef ARH_PROFILE
      static int step_gex_req = 0;
      tick_t end_gex_req = ticks_now();
      if (my_worker_local() == 0) {
        update_average(ticks_gex_req, end_gex_req - start_gex_req, ++step_gex_req);
      }
#endif
    }
#ifdef ARH_PROFILE
    else {
      static int step = 0;
      tick_t end = ticks_now();
      if (my_worker_local() == 0) {
        update_average(ticks_agg_buf_npop, end - start, ++step);
      }
    }
#endif

    return std::move(future);
  }
}

#endif //BCL_ARH_AM_AGGREGATE_HPP
