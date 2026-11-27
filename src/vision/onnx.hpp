// ONNX Runtime, wrapped: one process-wide environment, and a Model per .onnx file.
// Sessions try DirectML (any DirectX 12 GPU) first and fall back to the CPU, per model, so one
// model the GPU can't run never takes the others down with it.
#pragma once

#include <memory>
#include <string>
#include <vector>

namespace racg::vision {

enum class Device { Cpu, DirectML };

struct Tensor {
  std::vector<float> data;
  std::vector<int64_t> shape;
};

class Model {
 public:
  // preferGpu false forces the CPU (Settings, or a GPU that misbehaved).
  Model(const std::wstring &path, bool preferGpu);
  ~Model();
  Model(const Model &) = delete;
  Model &operator=(const Model &) = delete;

  // Inputs in the model's input order; returns every output in the model's output order.
  std::vector<Tensor> run(const std::vector<const Tensor *> &inputs);

  Device device() const { return device_; }
  const std::string &name() const { return name_; }

 private:
  struct Impl;
  std::unique_ptr<Impl> d_;
  Device device_ = Device::Cpu;
  std::string name_;
};

// Throws std::runtime_error with a readable message when a model can't be loaded.
const char *deviceName(Device d);

}  // namespace racg::vision
