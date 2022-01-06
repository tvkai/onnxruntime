// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "core/providers/shared_library/provider_api.h"
#include "layer_norm.h"
#include "layer_norm_impl.h"
#include "core/providers/cuda/cuda_common.h"

namespace onnxruntime {
namespace contrib {
namespace cuda {

#define REGISTER_KERNEL_TYPED(T, T1, U)                                                                               \
  ONNX_OPERATOR_TYPED_KERNEL_EX(LayerNormalization, kOnnxDomain, 1, T##_##T1##_##U, kCudaExecutionProvider,           \
                                (*KernelDefBuilder::Create())                                                         \
                                    .TypeConstraint("T", DataTypeImpl::GetTensorType<T>())                            \
                                    .TypeConstraint("T1", DataTypeImpl::GetTensorType<T1>())                          \
                                    .TypeConstraint("U", DataTypeImpl::GetTensorType<U>()),                           \
                                LayerNorm<T, T1, U, false>);                                                          \
  ONNX_OPERATOR_TYPED_KERNEL_EX(SimplifiedLayerNormalization, kOnnxDomain, 1, T##_##T1##_##U, kCudaExecutionProvider, \
                                (*KernelDefBuilder::Create())                                                         \
                                    .TypeConstraint("T", DataTypeImpl::GetTensorType<T>())                            \
                                    .TypeConstraint("T1", DataTypeImpl::GetTensorType<T1>())                          \
                                    .TypeConstraint("U", DataTypeImpl::GetTensorType<U>()),                           \
                                LayerNorm<T, T1, U, true>);

REGISTER_KERNEL_TYPED(float, float, float)
REGISTER_KERNEL_TYPED(double, double, double)
REGISTER_KERNEL_TYPED(MLFloat16, MLFloat16, float)
REGISTER_KERNEL_TYPED(float, MLFloat16, float)
#if defined(CUDA_VERSION) && CUDA_VERSION >= 11000
REGISTER_KERNEL_TYPED(BFloat16, BFloat16, float)
#endif

template <typename T, typename T1, typename U, bool simplified>
LayerNorm<T, T1, U, simplified>::LayerNorm(const OpKernelInfo& op_kernel_info) : CudaKernel(op_kernel_info) {
  ORT_ENFORCE(op_kernel_info.GetAttr("axis", &axis_).IsOK());
  float tmp_epsilon;
  ORT_ENFORCE(op_kernel_info.GetAttr<float>("epsilon", &tmp_epsilon).IsOK());
  epsilon_ = tmp_epsilon;
}

template <typename T, typename T1, typename U, bool simplified>
Status LayerNorm<T, T1, U, simplified>::ComputeInternal(OpKernelContext* ctx) const {
  typedef typename ToCudaType<T>::MappedType CudaT;
  typedef typename ToCudaType<T1>::MappedType CudaT1;
  typedef typename ToCudaType<U>::MappedType CudaU;
  //Inputs
  const Tensor* X = ctx->Input<Tensor>(0);
  const Tensor* scale = ctx->Input<Tensor>(1);
  const Tensor* bias = ctx->Input<Tensor>(2);

  auto X_data = reinterpret_cast<const CudaT*>(X->template Data<T>());
  auto scale_data = reinterpret_cast<const CudaT1*>(scale->template Data<T1>());
  auto bias_data = (simplified || (nullptr == bias)) ? nullptr : reinterpret_cast<const CudaT1*>(bias->template Data<T1>());

  const TensorShape& x_shape = X->Shape();
  // Sometimes due to conversion issue, the input 'X' has no data which is a case that cuda kernel cannot handle.
  // Provide more error infomation here instead of CUDA errors.
  if (X->SizeInBytes() == 0) {
    return ORT_MAKE_STATUS(ONNXRUNTIME, INVALID_ARGUMENT, "Inputs 'X' has no data from upstream nodes");
  }

  const int64_t axis = HandleNegativeAxis(axis_, x_shape.NumDimensions());

  int n1 = gsl::narrow<int>(x_shape.SizeToDimension(axis));
  int n2 = gsl::narrow<int>(x_shape.SizeFromDimension(axis));

  ORT_ENFORCE(n2 != 1, "n2 should not be 1");

  // Outputs
  Tensor* Y = ctx->Output(0, x_shape);
  auto Y_data = reinterpret_cast<CudaT*>(Y->template MutableData<T>());

  //Mean and variance
  std::vector<int64_t> mean_inv_std_var_dim;
  for (int i = 0; i < static_cast<int>(x_shape.NumDimensions()); ++i) {
    if (i < axis) {
      mean_inv_std_var_dim.emplace_back(x_shape.GetDims()[i]);
    } else {
      mean_inv_std_var_dim.emplace_back(1);
    }
  }
  int output_index = 1;

  CudaU* mean_data = nullptr;
  if (!simplified) {
    Tensor* mean = ctx->Output(output_index++, TensorShape(mean_inv_std_var_dim));
    if (mean != nullptr) {
      mean_data = reinterpret_cast<CudaU*>(mean->template MutableData<U>());
    }
  }

  Tensor* var = ctx->Output(output_index, TensorShape(mean_inv_std_var_dim));
  CudaU* inv_var_data = nullptr;
  if (var != nullptr) {
    inv_var_data = reinterpret_cast<CudaU*>(var->template MutableData<U>());
  }

  HostApplyLayerNorm<CudaT, CudaT1, CudaU, simplified>(GetDeviceProp(), Stream(), Y_data, mean_data, inv_var_data,
                                                       X_data, n1, n2, epsilon_, scale_data, bias_data);
  return Status::OK();
}

}  //namespace cuda
}  // namespace contrib
}  // namespace onnxruntime
