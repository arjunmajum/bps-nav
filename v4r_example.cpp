#include <torch/extension.h>
#include <ATen/ATen.h>
#include <ATen/cuda/CUDAContext.h>

#include <cuda.h>
#include <cuda_runtime.h>

#include <v4r.hpp>

#include <array>
#include <fstream>
#include <vector>

using namespace std;
using namespace v4r;

namespace py = pybind11;

static vector<glm::mat4> readViews(const string &p);

// Create a tensor that references this memory
static at::Tensor convertToTensor(void *dev_ptr)
{
    array<int64_t, 4> sizes {{32, 256, 256, 4}};

    // This would need to be more precise for multi gpu machines
    auto options = torch::TensorOptions().dtype(torch::kUInt8).device(torch::kCUDA);

    return torch::from_blob(dev_ptr, sizes, options);
}

class PyTorchSync {
public:
    PyTorchSync(RenderSync &&sync)
        : sync_(move(sync))
    {}

    void wait()
    {
        // Get the current CUDA stream from pytorch and force it to wait
        // on the renderer to finish
        cudaStream_t cuda_strm = at::cuda::getCurrentCUDAStream().stream();
        sync_.gpuWait(cuda_strm);
    }

private:
    RenderSync sync_;
};

class V4RExample {
public:
    V4RExample(const string &scene_path, const string &views_path)
        : renderer_({
              0,  // gpuID
              1,  // numLoaders
              1,  // numStreams
              32, // batchSize
              256, // imgWidth,
              256, // imgHeight
              glm::mat4(
                  1, 0, 0, 0,
                  0, -1.19209e-07, -1, 0,
                  0, 1, -1.19209e-07, 0,
                  0, 0, 0, 1
              ) // Habitat coordinate txfm matrix
          }),
          loader_(renderer_.makeLoader()),
          cmd_strm_(renderer_.makeCommandStream()),
          color_batch_(convertToTensor(cmd_strm_.getColorDevPtr())),
          views_(readViews(views_path)),
          loaded_scenes_(),
          view_cnt_(0)
    {
        loaded_scenes_.emplace_back(loader_.loadScene(scene_path));

        for (int batch_idx = 0; batch_idx < 32; batch_idx++) {
            cmd_strm_.initState(batch_idx, loaded_scenes_.back(),
                                90, 0.01, 1000);
        }
    }

    ~V4RExample()
    {
        for (auto &scene : loaded_scenes_) {
            loader_.dropScene(move(scene));
        }
    }

    at::Tensor getColorTensor() const { return color_batch_; }

    PyTorchSync render()
    {
        for (int batch_idx = 0; batch_idx < 32; batch_idx++) {
            cmd_strm_.setCameraView(batch_idx, views_[view_cnt_++]);
        }

        auto sync = cmd_strm_.render();

        return PyTorchSync(move(sync));
    }

private:
    BatchRenderer renderer_;
    SceneLoader loader_;
    CommandStream cmd_strm_;
    at::Tensor color_batch_;
    vector<glm::mat4> views_;
    vector<SceneHandle> loaded_scenes_;
    uint64_t view_cnt_;
};

vector<glm::mat4> readViews(const string &p)
{
    ifstream dump_file(p, ios::binary);

    vector<glm::mat4> views;

    for (size_t i = 0; i < 30000; i++) {
        float raw[16];
        dump_file.read((char *)raw, sizeof(float)*16);
        views.emplace_back(glm::inverse(
                glm::mat4(raw[0], raw[1], raw[2], raw[3],
                          raw[4], raw[5], raw[6], raw[7],
                          raw[8], raw[9], raw[10], raw[11],
                          raw[12], raw[13], raw[14], raw[15])));
    }

    return views;
}

PYBIND11_MODULE(TORCH_EXTENSION_NAME, m) {
    py::class_<V4RExample>(m, "V4RExample")
        .def(py::init<const string &, const string &>())
        .def("render", &V4RExample::render)
        .def("getColorTensor", &V4RExample::getColorTensor);

    py::class_<PyTorchSync>(m, "PyTorchSync")
        .def("wait", &PyTorchSync::wait);
}
