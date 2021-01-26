// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "orttraining/training_ops/cpu/controlflow/yield.h"
#include "core/framework/op_kernel_context_internal.h"
#include "core/providers/cpu/tensor/utils.h"
#include "orttraining/training_ops/cpu/controlflow/event_pool.h"
#include "orttraining/training_ops/cpu/controlflow/message_queue.h"

namespace onnxruntime {
namespace contrib {

ONNX_OPERATOR_KERNEL_EX(Yield, kMSDomain, 1, kCpuExecutionProvider,
                        KernelDefBuilder().TypeConstraint("T", DataTypeImpl::AllFixedSizeTensorTypes()), Yield);

Status Yield::Compute(OpKernelContext* ctx) const {
  auto* context_ = static_cast<OpKernelContextInternal*>(ctx);
  // FW output should be ready by this point, they are currently exposed as graph output
  // !!! Potential TODO here: If graph output approach doesn't work, need to place the Yield Input tensors into some shared location
  if (push_input_) {
    ORT_ENFORCE(ctx->InputCount() == 1);
    // auto p_tensor = ctx->Input<Tensor>(0);
    // Tensor* c_tensor = p_tensor;
    // void* data  = c_tensor->MutableDataRaw();
    // auto ort_value = onnxruntime::make_unique<OrtValue>();
    // ort_value->Init(data, DataTypeImpl::GetType<Tensor>(),
    //                 DataTypeImpl::GetType<Tensor>()->GetDeleteFunc());
    auto ort_value = context_->GetInputMLValue(0);
    onnxruntime::contrib::OrtMessageQueue::GetInstance().PushOutputGrad(*ort_value);
  }
  // single event for InferenceSession::RunInBackgroundAndWaitForYield() that FW graph is done
  const int64_t main_thread_event_id = 0;
  OrtEventPool::GetInstance().SignalEvent(main_thread_event_id);

  // wait for event from InferenceSession::ContinueRunInBackground() to continue the BW graph
  const int64_t background_thread_event_id = 1;
  OrtEventPool::GetInstance().ResetAndWaitEvent(background_thread_event_id);

  // Get output grad from somewhere and prepare Op outputs.
  for (int i_out = 0; i_out < ctx->OutputCount(); ++i_out) {
    OrtValue value = OrtMessageQueue::GetInstance().PopOutputGrad();
    const Tensor& X = value.Get<Tensor>();
    const TensorShape& data_shape = X.Shape();
    Tensor* Y = ctx->Output(i_out, data_shape);
    CopyCpuTensor(&X, Y);
  }

  return Status::OK();
}

}  // namespace contrib
}  // namespace onnxruntime