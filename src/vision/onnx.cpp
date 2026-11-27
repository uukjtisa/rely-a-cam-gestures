#include "vision/onnx.hpp"

#include <dml_provider_factory.h>
#include <onnxruntime_cxx_api.h>

#include <filesystem>
#include <mutex>
#include <stdexcept>

namespace racg::vision {

namespace {
Ort::Env &env() {
  static Ort::Env e(ORT_LOGGING_LEVEL_WARNING, "rely-a-cam-gestures");
  return e;
}
}  // namespace

const char *deviceName(Device d) { return d == Device::DirectML ? "GPU (DirectML)" : "CPU"; }

struct Model::Impl {
  std::unique_ptr<Ort::Session> session;
  std::vector<std::string> inNames, outNames;
  std::vector<const char *> inPtr, outPtr;
  std::mutex mu;   // a Session may run from several threads; one run at a time keeps memory flat
};

Model::Model(const std::wstring &path, bool preferGpu) : d_(std::make_unique<Impl>()) {
  name_ = std::filesystem::path(path).stem().string();
  auto make = [&](bool gpu) {
    Ort::SessionOptions so;
    so.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    so.SetIntraOpNumThreads(2);
    if (gpu) {
      // DirectML can't use memory patterns or parallel execution (ORT's documented requirement).
      so.DisableMemPattern();
      so.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
      Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_DML(so, 0));
    }
    return std::make_unique<Ort::Session>(env(), path.c_str(), so);
  };
  if (preferGpu) {
    try {
      d_->session = make(true);
      device_ = Device::DirectML;
    } catch (const Ort::Exception &) {
      d_->session.reset();
    }
  }
  if (!d_->session) {
    try {
      d_->session = make(false);
      device_ = Device::Cpu;
    } catch (const Ort::Exception &e) {
      throw std::runtime_error("Can't load model " + name_ + ": " + e.what());
    }
  }
  Ort::AllocatorWithDefaultOptions alloc;
  for (size_t i = 0; i < d_->session->GetInputCount(); ++i)
    d_->inNames.emplace_back(d_->session->GetInputNameAllocated(i, alloc).get());
  for (size_t i = 0; i < d_->session->GetOutputCount(); ++i)
    d_->outNames.emplace_back(d_->session->GetOutputNameAllocated(i, alloc).get());
  for (auto &n : d_->inNames) d_->inPtr.push_back(n.c_str());
  for (auto &n : d_->outNames) d_->outPtr.push_back(n.c_str());
}

Model::~Model() = default;

std::vector<Tensor> Model::run(const std::vector<const Tensor *> &inputs) {
  if (inputs.size() != d_->inPtr.size())
    throw std::runtime_error(name_ + ": expected " + std::to_string(d_->inPtr.size()) + " inputs");
  auto mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
  std::vector<Ort::Value> in;
  in.reserve(inputs.size());
  for (const Tensor *t : inputs)
    in.push_back(Ort::Value::CreateTensor<float>(mem, const_cast<float *>(t->data.data()), t->data.size(),
                                                 t->shape.data(), t->shape.size()));
  std::lock_guard lock(d_->mu);
  auto out = d_->session->Run(Ort::RunOptions{nullptr}, d_->inPtr.data(), in.data(), in.size(),
                              d_->outPtr.data(), d_->outPtr.size());
  std::vector<Tensor> res(out.size());
  for (size_t i = 0; i < out.size(); ++i) {
    auto info = out[i].GetTensorTypeAndShapeInfo();
    res[i].shape = info.GetShape();
    const float *p = out[i].GetTensorData<float>();
    res[i].data.assign(p, p + info.GetElementCount());
  }
  return res;
}

}  // namespace racg::vision
