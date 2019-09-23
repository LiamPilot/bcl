#include <bcl/bcl.hpp>
#include <bcl/backends/experimental/nvshmem/backend.hpp>
#include <bcl/containers/experimental/cuda/DuplQueue.hpp>
#include <bcl/containers/experimental/cuda/launch_kernel.cuh>

#include <chrono>

#define NUM_INSERTS 2*1024

int main(int argc, char** argv) {
  BCL::init(16);

  printf("Hello, world! I am rank %lu/%lu\n",
         BCL::rank(), BCL::nprocs());

  BCL::cuda::init();

  size_t num_inserts = NUM_INSERTS;
  size_t insert_size = 64;

  BCL::cuda::DuplQueue<int> queue(0, num_inserts*insert_size);

  BCL::cuda::device_vector<int, BCL::cuda::bcl_allocator<int>> values(insert_size);
  // BCL::cuda::device_vector<int> values(insert_size);
  std::vector<int> values_local(insert_size, BCL::rank());
  values.assign(values_local.begin(), values_local.end());

  BCL::cuda::barrier();
  auto begin = std::chrono::high_resolution_clock::now();

  BCL::cuda::global_launch(num_inserts,
                     [] __device__ (size_t idx, BCL::cuda::DuplQueue<int>& queue,
                                    BCL::cuda::device_vector<int, BCL::cuda::bcl_allocator<int>>& values) {
                                    // BCL::cuda::device_vector<int>& values) {
                       bool success = queue.push(values.data(), values.size());
                       if (!success) {
                         printf("AGH! I have failed!\n");
                       }
                     }, queue, values);

  cudaDeviceSynchronize();
  BCL::cuda::barrier();

  if (BCL::rank() == 0) {
    BCL::cuda::launch(num_inserts,
                      [] __device__ (size_t idx, BCL::cuda::DuplQueue<int>& queue) {
                        int value = 12;
                        bool success = queue.local_pop(value);
                        printf("%lu: %d (%s)\n", idx, value, (success) ? "success" : "failure");
                      }, queue);
    cudaDeviceSynchronize();
  }
  BCL::cuda::barrier();

  BCL::print("Here...\n");

  BCL::cuda::barrier();
  BCL::print("After barrier...\n");
  auto end = std::chrono::high_resolution_clock::now();

  BCL::finalize();
  return 0;
}
