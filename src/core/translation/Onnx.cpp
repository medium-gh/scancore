#include "Onnx.h"

#include "core/Http.h"

#include <QLibrary>
#include <QMutex>
#include <QThread>

#include <onnxruntime_c_api.h>

#include <algorithm>

namespace sc::onnx {

namespace {

QMutex g_mutex;
const OrtApi *g_api = nullptr;
OrtEnv *g_env = nullptr;

void check(OrtStatus *status) {
    if (!status)
        return;
    const auto message = QString::fromUtf8(g_api->GetErrorMessage(status));
    g_api->ReleaseStatus(status);
    throw Error(QStringLiteral("On-device model: %1").arg(message));
}

} // namespace

bool load(const QString &path, QString *error) {
    QMutexLocker lock(&g_mutex);
    if (g_api)
        return true;
#if !QT_CONFIG(library)
    // No libraries to load at run time (a Qt built without them).
    Q_UNUSED(path)
    if (error)
        *error = QStringLiteral("On-device models are not available on this system.");
    return false;
#else
    // Kept loaded for the rest of the run.
    auto *library = new QLibrary(path);
    using GetApiBase = const OrtApiBase *(ORT_API_CALL *)();
    const auto base = reinterpret_cast<GetApiBase>(library->resolve("OrtGetApiBase"));
    const OrtApi *api = base ? base()->GetApi(ORT_API_VERSION) : nullptr;
    if (!api) {
        if (error)
            *error = library->isLoaded() ? QStringLiteral("This ONNX Runtime is too old.") : library->errorString();
        delete library;
        return false;
    }
    OrtEnv *env = nullptr;
    if (auto *status = api->CreateEnv(ORT_LOGGING_LEVEL_ERROR, "scancore", &env)) {
        if (error)
            *error = QString::fromUtf8(api->GetErrorMessage(status));
        api->ReleaseStatus(status);
        return false;
    }
    g_env = env;
    g_api = api;
    return true;
#endif
}

bool loaded() {
    QMutexLocker lock(&g_mutex);
    return g_api != nullptr;
}

int64_t Tensor::count() const {
    int64_t n = 1;
    for (const auto d : shape)
        n *= d;
    return n;
}

struct Session::Impl {
    OrtSession *session = nullptr;
    OrtMemoryInfo *memory = nullptr;
};

Session::Session(const QString &model) : d(std::make_unique<Impl>()) {
    if (!loaded())
        throw Error(QStringLiteral("The on-device translation pack is not installed."));
    OrtSessionOptions *options = nullptr;
    check(g_api->CreateSessionOptions(&options));
    // Half the cores: the reader and the scene keep theirs.
    check(g_api->SetIntraOpNumThreads(options, std::max(1, QThread::idealThreadCount() / 2)));
    check(g_api->SetSessionGraphOptimizationLevel(options, ORT_ENABLE_ALL));
#ifdef _WIN32
    const auto path = model.toStdWString();
#else
    const auto path = model.toStdString();
#endif
    OrtStatus *status = g_api->CreateSession(g_env, path.c_str(), options, &d->session);
    g_api->ReleaseSessionOptions(options);
    check(status);
    check(g_api->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &d->memory));

    OrtAllocator *allocator = nullptr;
    check(g_api->GetAllocatorWithDefaultOptions(&allocator));
    size_t count = 0;
    check(g_api->SessionGetInputCount(d->session, &count));
    for (size_t i = 0; i < count; ++i) {
        char *name = nullptr;
        check(g_api->SessionGetInputName(d->session, i, allocator, &name));
        m_inputs.emplace_back(name);
        check(g_api->AllocatorFree(allocator, name));
    }
    check(g_api->SessionGetOutputCount(d->session, &count));
    for (size_t i = 0; i < count; ++i) {
        char *name = nullptr;
        check(g_api->SessionGetOutputName(d->session, i, allocator, &name));
        m_outputs.emplace_back(name);
        check(g_api->AllocatorFree(allocator, name));
    }
}

Session::~Session() {
    if (d->memory)
        g_api->ReleaseMemoryInfo(d->memory);
    if (d->session)
        g_api->ReleaseSession(d->session);
}

std::vector<Tensor> Session::run(const std::vector<std::pair<std::string, Tensor>> &inputs, const std::vector<std::string> &outputs) const {
    std::vector<OrtValue *> in;
    std::vector<const char *> inNames;
    const auto releaseAll = [](std::vector<OrtValue *> &values) {
        for (auto *v : values)
            if (v)
                g_api->ReleaseValue(v);
        values.clear();
    };
    try {
        for (const auto &[name, tensor] : inputs) {
            OrtValue *value = nullptr;
            void *data = tensor.isInt ? static_cast<void *>(const_cast<int64_t *>(tensor.ints.data()))
                                      : static_cast<void *>(const_cast<float *>(tensor.floats.data()));
            const size_t bytes = tensor.isInt ? tensor.ints.size() * sizeof(int64_t) : tensor.floats.size() * sizeof(float);
            check(g_api->CreateTensorWithDataAsOrtValue(d->memory, data, bytes, tensor.shape.data(), tensor.shape.size(),
                                                        tensor.isInt ? ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64 : ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
                                                        &value));
            in.push_back(value);
            inNames.push_back(name.c_str());
        }
        const auto &wanted = outputs.empty() ? m_outputs : outputs;
        std::vector<const char *> outNames;
        for (const auto &name : wanted)
            outNames.push_back(name.c_str());
        std::vector<OrtValue *> out(wanted.size(), nullptr);
        check(g_api->Run(d->session, nullptr, inNames.data(), in.data(), in.size(), outNames.data(), outNames.size(), out.data()));
        releaseAll(in);

        std::vector<Tensor> result;
        try {
            for (auto *value : out) {
                OrtTensorTypeAndShapeInfo *info = nullptr;
                check(g_api->GetTensorTypeAndShape(value, &info));
                size_t dims = 0;
                check(g_api->GetDimensionsCount(info, &dims));
                Tensor t;
                t.shape.resize(dims);
                check(g_api->GetDimensions(info, t.shape.data(), dims));
                ONNXTensorElementDataType type{};
                check(g_api->GetTensorElementType(info, &type));
                g_api->ReleaseTensorTypeAndShapeInfo(info);
                void *data = nullptr;
                check(g_api->GetTensorMutableData(value, &data));
                const auto n = size_t(std::max<int64_t>(0, t.count()));
                if (type == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64) {
                    t.isInt = true;
                    t.ints.assign(static_cast<int64_t *>(data), static_cast<int64_t *>(data) + n);
                } else if (type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
                    t.floats.assign(static_cast<float *>(data), static_cast<float *>(data) + n);
                } else {
                    throw Error(QStringLiteral("On-device model: an output of an unexpected type."));
                }
                result.push_back(std::move(t));
            }
        } catch (...) {
            releaseAll(out);
            throw;
        }
        releaseAll(out);
        return result;
    } catch (...) {
        releaseAll(in);
        throw;
    }
}

} // namespace sc::onnx
