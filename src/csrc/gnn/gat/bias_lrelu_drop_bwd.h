/******************************************************************************
 * Copyright (c) 2022 Intel Corporation - All rights reserved.                *
 *                                                                            *
 * For information on the license, see the LICENSE file.                      *
 * Further information: https://github.com/libxsmm/tpp-pytorch-extension/     *
 * SPDX-License-Identifier: BSD-3-Clause                                      *
 ******************************************************************************/
/* Authors: Ramanarayan Mohanty, Sasikanth Avancha (Intel Corp.)
 ******************************************************************************/

RECORD_FUNCTION("bias_lrelu_drop_bwd", std::vector<c10::IValue>());

int i = 0;
auto t_grad_out = inputs[i++];
auto t_inp = inputs[i++];
auto t_lrelu_mask = inputs[i++];
auto t_dp_mask = (p > 0) ? inputs[i++] : at::empty(0, at::kShort);

auto in_sizes = t_grad_out.sizes();
auto N = in_sizes[0];
long K = in_sizes[1];
auto dK = (K + 15) / 16;

auto t_grad_in = t_grad_out.new_empty({N, K});
auto t_grad_bias = t_grad_out.new_empty({K});

auto grad_out = GetVLAPtr<T>(t_grad_out, {K});
auto inp = GetVLAPtr<T>(t_inp, {K});
auto lrelu_mask = GetVLAPtr<short>(t_lrelu_mask, {dK});

auto grad_in = GetVLAPtr<T>(t_grad_in, {K});
auto grad_bias = GetVLAPtr<T>(t_grad_bias, {K});

auto leaky_relu_bwd_tpp = SCOPEIT(LeakyReLUBwdTPP<T>(1, K, alpha), ACT);
auto grad_bias_tpp = SCOPEIT(GradBiasTPP<T>(1, K), BIAS);
auto set_zero_tpp = SCOPEIT(SetZeroTPP<float>(K), EW_ZERO);
int threads = omp_get_max_threads();

if (p > 0) {
  auto dropout_bwd_tpp = SCOPEIT(DropOutBwdTPP<T>(1, K, p), DROPOUT);
  auto dp_mask = GetVLAPtr<short>(t_dp_mask, {dK});
  RECORD_SCOPE(gdo_bias_lrelu_drop, {t_grad_out});
  {
    tensor_set_zero(1, K, t_grad_bias);
    float* bias_ptrs[threads];
    RECORD_FUNCTION("parallel_for", std::vector<c10::IValue>());
#pragma omp parallel
    {
      int tid = omp_get_thread_num();
      float prv_grad_bias[1][K];
      bias_ptrs[tid] = prv_grad_bias[0];
      set_zero_tpp(prv_grad_bias[0]);
#pragma omp for
      for (int n = 0; n < N; n++) {
        dropout_bwd_tpp(grad_out[n], grad_in[n], dp_mask[n]);
        leaky_relu_bwd_tpp(grad_in[n], grad_in[n], inp[n], lrelu_mask[n]);
        grad_bias_tpp(grad_in[n], prv_grad_bias[0]);
      }
      omp_reduce_buf(threads, K, bias_ptrs, grad_bias[0]);
    }
  }
} else {
  RECORD_SCOPE(gdo_bias_lrelu_drop, {t_grad_out});
  {
    tensor_set_zero(1, K, t_grad_bias);
    float* bias_ptrs[threads];
    RECORD_FUNCTION("parallel_for", std::vector<c10::IValue>());
#pragma omp parallel
    {
      int tid = omp_get_thread_num();
      float prv_grad_bias[1][K];
      bias_ptrs[tid] = prv_grad_bias[0];
      set_zero_tpp(prv_grad_bias[0]);
#pragma omp for
      for (int n = 0; n < N; n++) {
        leaky_relu_bwd_tpp(grad_out[n], grad_in[n], inp[n], lrelu_mask[n]);
        grad_bias_tpp(grad_in[n], prv_grad_bias[0]);
      }
      omp_reduce_buf(threads, K, bias_ptrs, grad_bias[0]);
    }
  }
}

return {t_grad_in, t_grad_bias};
