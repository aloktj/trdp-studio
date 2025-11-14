#include "trdp/TrdpEngine.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef __linux__
#include <dlfcn.h>
#endif

#if defined(__has_include)
#if __has_include(<trdp/trdp_if_light.h>)
#define TRDP_HAS_NATIVE_API 1
#include <trdp/iec61375-2-3.h>
#include <trdp/trdp_if_light.h>
#else
#define TRDP_HAS_NATIVE_API 0
#endif
#else
#define TRDP_HAS_NATIVE_API 0
#endif

#include "db/Database.hpp"

namespace {

std::string trimCopy(const std::string &value) {
    auto begin = value.find_first_not_of(" \t\n\r");
    if (begin == std::string::npos) {
        return "";
    }
    auto end = value.find_last_not_of(" \t\n\r");
    return value.substr(begin, end - begin + 1);
}

std::string toLowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

int safeStoi(const std::string &value, int fallback = 0) {
    try {
        return std::stoi(value);
    } catch (...) {
        return fallback;
    }
}

struct XmlElement {
    std::unordered_map<std::string, std::string> attributes;
    std::string body;
};

std::unordered_map<std::string, std::string> parseAttributes(const std::string &raw) {
    std::unordered_map<std::string, std::string> result;
    static const std::regex attr_regex{R"(([A-Za-z0-9_:\-\.]+)\s*=\s*\"([^\"]*)\")"};
    auto begin = std::sregex_iterator(raw.begin(), raw.end(), attr_regex);
    auto end = std::sregex_iterator();
    for (auto it = begin; it != end; ++it) {
        result[it->str(1)] = it->str(2);
    }
    return result;
}

std::vector<XmlElement> extractElements(const std::string &xml, const std::string &tag) {
    std::vector<XmlElement> elements;
    const std::string open = "<" + tag;
    const std::string close = "</" + tag + ">";
    std::size_t pos = 0;
    while ((pos = xml.find(open, pos)) != std::string::npos) {
        const std::size_t head_start = pos + open.size();
        std::size_t closing = xml.find('>', head_start);
        if (closing == std::string::npos) {
            break;
        }
        bool self_closing = closing > head_start && xml[closing - 1] == '/';
        std::string attr_segment = xml.substr(head_start, closing - head_start);
        if (self_closing && !attr_segment.empty()) {
            attr_segment.pop_back();
        }
        XmlElement element;
        element.attributes = parseAttributes(attr_segment);
        if (!self_closing) {
            auto close_pos = xml.find(close, closing + 1);
            if (close_pos == std::string::npos) {
                break;
            }
            element.body = xml.substr(closing + 1, close_pos - (closing + 1));
            pos = close_pos + close.size();
        } else {
            pos = closing + 1;
        }
        elements.push_back(std::move(element));
    }
    return elements;
}

std::vector<uint8_t> parseHexPayload(const std::string &raw) {
    std::string filtered;
    filtered.reserve(raw.size());
    for (char ch : raw) {
        if (std::isxdigit(static_cast<unsigned char>(ch))) {
            filtered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        }
    }
    if (filtered.empty()) {
        return {};
    }
    if (filtered.size() % 2 != 0U) {
        filtered.insert(filtered.begin(), '0');
    }
    std::vector<uint8_t> payload;
    payload.reserve(filtered.size() / 2);
    for (std::size_t i = 0; i < filtered.size(); i += 2) {
        auto byte_str = filtered.substr(i, 2);
        uint8_t value = static_cast<uint8_t>(std::stoi(byte_str, nullptr, 16));
        payload.push_back(value);
    }
    return payload;
}

}  // namespace

namespace trdp::stack {

struct TrdpEngine::PdRuntimeState {
    TrdpEngine *engine {nullptr};
    int id {0};
    std::string name;
    bool is_outgoing {true};
    int cycle_ms {0};
    std::string destination;
    std::string source;
    std::vector<uint8_t> payload;
    std::chrono::steady_clock::time_point next_cycle;
    void *native_handle {nullptr};
};

struct TrdpEngine::MdRuntimeState {
    TrdpEngine *engine {nullptr};
    int runtime_id {0};
    int last_message_id {0};
    std::string name;
    std::string source;
    std::string destination;
    std::vector<uint8_t> last_payload;
    void *native_handle {nullptr};
};

class TrdpEngine::TrdpStackAdapter {
public:
    explicit TrdpStackAdapter(TrdpEngine &engine) : engine_(engine) {}
    ~TrdpStackAdapter() { shutdown(); }

    bool initialize(const network::NetworkConfig &cfg) {
        network_cfg_ = cfg;
#ifdef __linux__
        native_available_ = loadNativeLibrary();
#else
        native_available_ = false;
#endif
        if (native_available_) {
            if (!initializeNativeSession(cfg)) {
                native_available_ = false;
            }
        }
        ready_ = true;
        return true;
    }

    void shutdown() {
        if (!ready_) {
            return;
        }
        if (native_available_) {
            shutdownNativeSession();
        }
        unloadNativeLibrary();
        ready_ = false;
    }

    bool registerPublisher(PdRuntimeState &state) {
#if TRDP_HAS_NATIVE_API
        if (native_available_) {
            auto handle = reinterpret_cast<PdPublisherHandle>(state.native_handle);
            if (handle == nullptr && tlp_publish_ != nullptr && native_session_ != nullptr) {
                TRDP_PUB_T pub_handle = nullptr;
                const TRDP_IP_ADDR_T src_ip = parseEndpointIp(state.source);
                const TRDP_IP_ADDR_T dest_ip = parseEndpointIp(state.destination);
                const UINT32 interval = static_cast<UINT32>(std::max(state.cycle_ms, 1)) * 1000U;
                const UINT8 *data_ptr = state.payload.empty() ? nullptr : state.payload.data();
                const UINT32 data_len = static_cast<UINT32>(state.payload.size());
                const TRDP_ERR_T err =
                    tlp_publish_(native_session_, &pub_handle, &state, nullptr, 0u, static_cast<UINT32>(state.id), 0u, 0u,
                                 src_ip, dest_ip, interval, 0u, TRDP_FLAGS_DEFAULT, data_ptr, data_len);
                if (err != TRDP_NO_ERR) {
                    std::cerr << "Failed to register PD publisher for comId " << state.id << std::endl;
                    return false;
                }
                state.native_handle = pub_handle;
            }
        }
#endif
        return true;
    }

    bool registerSubscriber(PdRuntimeState &state) {
#if TRDP_HAS_NATIVE_API
        if (native_available_) {
            auto handle = reinterpret_cast<PdSubscriberHandle>(state.native_handle);
            if (handle == nullptr && tlp_subscribe_ != nullptr && native_session_ != nullptr) {
                TRDP_SUB_T sub_handle = nullptr;
                const TRDP_IP_ADDR_T src_ip = parseEndpointIp(state.source);
                const TRDP_IP_ADDR_T dest_ip = parseEndpointIp(state.destination);
                const UINT32 timeout = static_cast<UINT32>(std::max(state.cycle_ms, 1)) * 1000U;
                const TRDP_ERR_T err = tlp_subscribe_(native_session_, &sub_handle, &state, &TrdpStackAdapter::pdNativeCallback,
                                                      0u, static_cast<UINT32>(state.id), 0u, 0u, src_ip, 0u, dest_ip,
                                                      TRDP_FLAGS_DEFAULT, timeout, TRDP_TO_KEEP_LAST_VALUE);
                if (err != TRDP_NO_ERR) {
                    std::cerr << "Failed to register PD subscriber for comId " << state.id << std::endl;
                    return false;
                }
                state.native_handle = sub_handle;
            }
        }
#endif
        return true;
    }

    bool registerMdEndpoint(MdRuntimeState &state) {
#if TRDP_HAS_NATIVE_API
        if (native_available_) {
            auto handle = reinterpret_cast<MdListenerHandle>(state.native_handle);
            if (handle == nullptr && tlm_addListener_ != nullptr && native_session_ != nullptr) {
                TRDP_LIS_T listener = nullptr;
                const TRDP_IP_ADDR_T src_ip = parseEndpointIp(state.source);
                const TRDP_IP_ADDR_T dest_ip = parseEndpointIp(state.destination);
                TRDP_URI_USER_T empty_uri = {0};
                const TRDP_ERR_T err = tlm_addListener_(native_session_, &listener, &state,
                                                        &TrdpStackAdapter::mdNativeCallback, static_cast<BOOL8>(1u),
                                                        static_cast<UINT32>(state.runtime_id), 0u, 0u, src_ip, 0u,
                                                        dest_ip, TRDP_FLAGS_DEFAULT, empty_uri, empty_uri);
                if (err != TRDP_NO_ERR) {
                    std::cerr << "Failed to register MD listener for runtime " << state.runtime_id << std::endl;
                    return false;
                }
                state.native_handle = listener;
            }
        }
#endif
        return true;
    }

    bool sendPd(PdRuntimeState &state, const std::vector<uint8_t> &payload) {
        state.payload = payload;
#if TRDP_HAS_NATIVE_API
        if (native_available_) {
            auto handle = reinterpret_cast<PdPublisherHandle>(state.native_handle);
            if (handle == nullptr) {
                if (!registerPublisher(state)) {
                    return false;
                }
                handle = reinterpret_cast<PdPublisherHandle>(state.native_handle);
            }
            if (handle != nullptr && tlp_put_ != nullptr && native_session_ != nullptr) {
                const UINT8 *data_ptr = payload.empty() ? nullptr : payload.data();
                const TRDP_ERR_T err = tlp_put_(native_session_, handle, data_ptr, static_cast<UINT32>(payload.size()));
                return err == TRDP_NO_ERR;
            }
            return false;
        }
#endif
        return true;
    }

    bool sendMd(MdRuntimeState &state, const std::vector<uint8_t> &payload, int message_id) {
        state.last_payload = payload;
        state.last_message_id = message_id;
#if TRDP_HAS_NATIVE_API
        if (native_available_) {
            if (tlm_notify_ != nullptr && native_session_ != nullptr) {
                const TRDP_IP_ADDR_T src_ip = parseEndpointIp(state.source);
                const TRDP_IP_ADDR_T dest_ip = parseEndpointIp(state.destination);
                TRDP_URI_USER_T empty_uri = {0};
                const UINT8 *data_ptr = payload.empty() ? nullptr : payload.data();
                const TRDP_ERR_T err =
                    tlm_notify_(native_session_, &state, &TrdpStackAdapter::mdNativeCallback,
                                static_cast<UINT32>(message_id), 0u, 0u, src_ip, dest_ip, TRDP_FLAGS_DEFAULT,
                                &md_config_.sendParam, data_ptr, static_cast<UINT32>(payload.size()), empty_uri,
                                empty_uri);
                return err == TRDP_NO_ERR;
            }
            return false;
        }
#endif
        return true;
    }

    bool iterate() {
#if TRDP_HAS_NATIVE_API
        if (native_available_) {
            if (tlc_process_ != nullptr && native_session_ != nullptr) {
                return tlc_process_(native_session_, nullptr, nullptr) == TRDP_NO_ERR;
            }
            return false;
        }
#endif
        return true;
    }

    bool ready() const { return ready_; }

private:
#if TRDP_HAS_NATIVE_API
    using NativeSessionHandle = TRDP_APP_SESSION_T;
    using PdPublisherHandle = TRDP_PUB_T;
    using PdSubscriberHandle = TRDP_SUB_T;
    using MdListenerHandle = TRDP_LIS_T;
#else
    using NativeSessionHandle = void *;
    using PdPublisherHandle = void *;
    using PdSubscriberHandle = void *;
    using MdListenerHandle = void *;
#endif

#ifdef __linux__
#if TRDP_HAS_NATIVE_API
    using InitFn = TRDP_ERR_T (*)(TRDP_PRINT_DBG_T, void *, const TRDP_MEM_CONFIG_T *);
    using TermFn = TRDP_ERR_T (*)(void);
    using OpenSessionFn = TRDP_ERR_T (*)(TRDP_APP_SESSION_T *, TRDP_IP_ADDR_T, TRDP_IP_ADDR_T,
                                         const TRDP_MARSHALL_CONFIG_T *, const TRDP_PD_CONFIG_T *,
                                         const TRDP_MD_CONFIG_T *, const TRDP_PROCESS_CONFIG_T *);
    using CloseSessionFn = TRDP_ERR_T (*)(TRDP_APP_SESSION_T);
    using ProcessFn = TRDP_ERR_T (*)(TRDP_APP_SESSION_T, TRDP_FDS_T *, INT32 *);
    using PdPublishFn = TRDP_ERR_T (*)(TRDP_APP_SESSION_T, TRDP_PUB_T *, const void *, TRDP_PD_CALLBACK_T, UINT32,
                                       UINT32, UINT32, UINT32, TRDP_IP_ADDR_T, TRDP_IP_ADDR_T, UINT32, UINT32,
                                       TRDP_FLAGS_T, const UINT8 *, UINT32);
    using PdSubscribeFn = TRDP_ERR_T (*)(TRDP_APP_SESSION_T, TRDP_SUB_T *, const void *, TRDP_PD_CALLBACK_T, UINT32,
                                         UINT32, UINT32, UINT32, TRDP_IP_ADDR_T, TRDP_IP_ADDR_T, TRDP_IP_ADDR_T,
                                         TRDP_FLAGS_T, UINT32, TRDP_TO_BEHAVIOR_T);
    using PdSendFn = TRDP_ERR_T (*)(TRDP_APP_SESSION_T, TRDP_PUB_T, const UINT8 *, UINT32);
    using MdSendFn = TRDP_ERR_T (*)(TRDP_APP_SESSION_T, const void *, TRDP_MD_CALLBACK_T, UINT32, UINT32, UINT32,
                                    TRDP_IP_ADDR_T, TRDP_IP_ADDR_T, TRDP_FLAGS_T, const TRDP_COM_PARAM_T *,
                                    const UINT8 *, UINT32, const TRDP_URI_USER_T, const TRDP_URI_USER_T);
    using MdSubscribeFn = TRDP_ERR_T (*)(TRDP_APP_SESSION_T, TRDP_LIS_T *, const void *, TRDP_MD_CALLBACK_T, BOOL8,
                                         UINT32, UINT32, UINT32, TRDP_IP_ADDR_T, TRDP_IP_ADDR_T, TRDP_IP_ADDR_T,
                                         TRDP_FLAGS_T, const TRDP_URI_USER_T, const TRDP_URI_USER_T);
#else
    using InitFn = int (*)(void **, const char *, const char *);
    using TermFn = int (*)(void *);
    using ProcessFn = int (*)(void *);
#endif
#endif

    bool loadNativeLibrary() {
#ifdef __linux__
        if (library_handle_ != nullptr) {
            return true;
        }
        library_handle_ = dlopen("libtrdp.so", RTLD_LAZY);
        if (library_handle_ == nullptr) {
            std::cerr << "TRDP native library not found; continuing in simulation mode." << std::endl;
            return false;
        }
#if TRDP_HAS_NATIVE_API
        tlc_init_ = reinterpret_cast<InitFn>(dlsym(library_handle_, "tlc_init"));
        tlc_openSession_ = reinterpret_cast<OpenSessionFn>(dlsym(library_handle_, "tlc_openSession"));
        tlc_closeSession_ = reinterpret_cast<CloseSessionFn>(dlsym(library_handle_, "tlc_closeSession"));
        tlc_terminate_ = reinterpret_cast<TermFn>(dlsym(library_handle_, "tlc_terminate"));
        tlc_process_ = reinterpret_cast<ProcessFn>(dlsym(library_handle_, "tlc_process"));
        tlp_publish_ = reinterpret_cast<PdPublishFn>(dlsym(library_handle_, "tlp_publish"));
        tlp_subscribe_ = reinterpret_cast<PdSubscribeFn>(dlsym(library_handle_, "tlp_subscribe"));
        tlp_put_ = reinterpret_cast<PdSendFn>(dlsym(library_handle_, "tlp_put"));
        tlm_notify_ = reinterpret_cast<MdSendFn>(dlsym(library_handle_, "tlm_notify"));
        tlm_addListener_ = reinterpret_cast<MdSubscribeFn>(dlsym(library_handle_, "tlm_addListener"));
        return tlc_init_ != nullptr && tlc_openSession_ != nullptr && tlc_closeSession_ != nullptr &&
               tlc_terminate_ != nullptr && tlc_process_ != nullptr && tlp_publish_ != nullptr &&
               tlp_subscribe_ != nullptr && tlp_put_ != nullptr && tlm_notify_ != nullptr &&
               tlm_addListener_ != nullptr;
#else
        tlc_init_ = reinterpret_cast<InitFn>(dlsym(library_handle_, "tlc_init"));
        tlc_terminate_ = reinterpret_cast<TermFn>(dlsym(library_handle_, "tlc_terminate"));
        tlc_process_ = reinterpret_cast<ProcessFn>(dlsym(library_handle_, "tlc_process"));
        return tlc_init_ != nullptr && tlc_terminate_ != nullptr && tlc_process_ != nullptr;
#endif
#else
        return false;
#endif
    }

    void unloadNativeLibrary() {
#ifdef __linux__
        if (library_handle_ != nullptr) {
            dlclose(library_handle_);
            library_handle_ = nullptr;
        }
        tlc_init_ = nullptr;
        tlc_terminate_ = nullptr;
#if TRDP_HAS_NATIVE_API
        tlc_openSession_ = nullptr;
        tlc_closeSession_ = nullptr;
        tlc_process_ = nullptr;
        tlp_publish_ = nullptr;
        tlp_subscribe_ = nullptr;
        tlp_put_ = nullptr;
        tlm_notify_ = nullptr;
        tlm_addListener_ = nullptr;
#else
        tlc_process_ = nullptr;
#endif
#endif
    }

    bool initializeNativeSession(const network::NetworkConfig &cfg) {
#ifdef __linux__
#if TRDP_HAS_NATIVE_API
        if (tlc_init_ == nullptr || tlc_openSession_ == nullptr || tlc_terminate_ == nullptr) {
            return false;
        }
        if (tlc_init_(&TrdpStackAdapter::logAdapterMessage, this, nullptr) != TRDP_NO_ERR) {
            return false;
        }
        configureSessionDefaults(cfg);
        const TRDP_IP_ADDR_T own_ip = parseEndpointIp(cfg.local_ip);
        const TRDP_ERR_T err =
            tlc_openSession_(&native_session_, own_ip, own_ip, nullptr, &pd_config_, &md_config_, &process_config_);
        if (err != TRDP_NO_ERR) {
            tlc_terminate_();
            native_session_ = nullptr;
            return false;
        }
        return true;
#else
        if (tlc_init_ == nullptr) {
            return false;
        }
        return tlc_init_(&native_session_, cfg.interface_name.c_str(), cfg.local_ip.c_str()) == 0;
#endif
#else
        (void)cfg;
        return false;
#endif
    }

    void shutdownNativeSession() {
#ifdef __linux__
#if TRDP_HAS_NATIVE_API
        if (native_session_ != nullptr && tlc_closeSession_ != nullptr) {
            tlc_closeSession_(native_session_);
            native_session_ = nullptr;
        }
        if (tlc_terminate_ != nullptr) {
            tlc_terminate_();
        }
#else
        if (native_session_ != nullptr && tlc_terminate_ != nullptr) {
            tlc_terminate_(native_session_);
            native_session_ = nullptr;
        }
#endif
#endif
    }

#if TRDP_HAS_NATIVE_API
    void configureSessionDefaults(const network::NetworkConfig &cfg) {
        pd_config_ = {};
        pd_config_.pfCbFunction = &TrdpStackAdapter::pdNativeCallback;
        pd_config_.pRefCon = this;
        pd_config_.sendParam = TRDP_PD_DEFAULT_SEND_PARAM;
        pd_config_.flags = TRDP_FLAGS_DEFAULT;
        pd_config_.timeout = 1000000u;
        pd_config_.toBehavior = TRDP_TO_KEEP_LAST_VALUE;
        pd_config_.port = static_cast<UINT16>(cfg.pd_port > 0 ? cfg.pd_port : 17224);

        md_config_ = {};
        md_config_.pfCbFunction = &TrdpStackAdapter::mdNativeCallback;
        md_config_.pRefCon = this;
        md_config_.sendParam = TRDP_MD_DEFAULT_SEND_PARAM;
        md_config_.flags = TRDP_FLAGS_DEFAULT;
        md_config_.replyTimeout = 2000000u;
        md_config_.confirmTimeout = 2000000u;
        md_config_.connectTimeout = 2000000u;
        md_config_.sendingTimeout = 2000000u;
        md_config_.udpPort = static_cast<UINT16>(cfg.md_port > 0 ? cfg.md_port : 17225);
        md_config_.tcpPort = md_config_.udpPort;
        md_config_.maxNumSessions = 16u;

        process_config_ = {};
        std::snprintf(process_config_.hostName, sizeof(process_config_.hostName), "trdp-studio");
        std::snprintf(process_config_.leaderName, sizeof(process_config_.leaderName), "trdp-leader");
        std::snprintf(process_config_.type, sizeof(process_config_.type), "studio");
        process_config_.cycleTime = 100000u;
        process_config_.priority = 0u;
        process_config_.options = TRDP_OPTION_BLOCK;
        process_config_.vlanId = 0u;
    }

    static void logAdapterMessage(void *, TRDP_LOG_T category, const CHAR8 *pTime, const CHAR8 *pFile, UINT16 line,
                                  const CHAR8 *message) {
        (void)category;
        (void)pTime;
        (void)pFile;
        (void)line;
        if (message != nullptr) {
            std::clog << "[TRDP] " << message << std::endl;
        }
    }

    static void pdNativeCallback(void *ref_con, TRDP_APP_SESSION_T, const TRDP_PD_INFO_T *info, UINT8 *payload,
                                 UINT32 size) {
        const uint8_t *data_ptr = payload;
        std::string src_ip;
        std::string dst_ip;
        if (info != nullptr) {
            src_ip = formatIp(info->srcIpAddr);
            dst_ip = formatIp(info->destIpAddr);
        }
        TrdpEngine::pdCallbackBridge(ref_con, data_ptr, size,
                                     src_ip.empty() ? nullptr : src_ip.c_str(),
                                     dst_ip.empty() ? nullptr : dst_ip.c_str());
    }

    static void mdNativeCallback(void *ref_con, TRDP_APP_SESSION_T, const TRDP_MD_INFO_T *info, UINT8 *payload,
                                 UINT32 size) {
        const uint8_t *data_ptr = payload;
        std::string src_ip;
        std::string dst_ip;
        if (info != nullptr) {
            src_ip = formatIp(info->srcIpAddr);
            dst_ip = formatIp(info->destIpAddr);
        }
        TrdpEngine::mdCallbackBridge(ref_con, data_ptr, size,
                                     src_ip.empty() ? nullptr : src_ip.c_str(),
                                     dst_ip.empty() ? nullptr : dst_ip.c_str());
    }

    static std::string formatIp(TRDP_IP_ADDR_T value) {
        if (value == 0u) {
            return {};
        }
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%u.%u.%u.%u",
                      (value >> 24) & 0xFFu, (value >> 16) & 0xFFu, (value >> 8) & 0xFFu, value & 0xFFu);
        return buffer;
    }

    static TRDP_IP_ADDR_T parseIp(const std::string &ip) {
        if (ip.empty()) {
            return 0u;
        }
        unsigned int octets[4];
        if (std::sscanf(ip.c_str(), "%u.%u.%u.%u", &octets[3], &octets[2], &octets[1], &octets[0]) != 4) {
            return 0u;
        }
        return (static_cast<TRDP_IP_ADDR_T>(octets[3] & 0xFFu) << 24) |
               (static_cast<TRDP_IP_ADDR_T>(octets[2] & 0xFFu) << 16) |
               (static_cast<TRDP_IP_ADDR_T>(octets[1] & 0xFFu) << 8) |
               static_cast<TRDP_IP_ADDR_T>(octets[0] & 0xFFu);
    }

    TRDP_IP_ADDR_T parseEndpointIp(const std::string &endpoint) const {
        const std::string ip = TrdpEngine::extractIp(endpoint);
        if (ip.empty()) {
            return parseIp(network_cfg_.local_ip);
        }
        return parseIp(ip);
    }
#endif

    TrdpEngine &engine_;
    network::NetworkConfig network_cfg_;
#ifdef __linux__
    void *library_handle_ {nullptr};
    InitFn tlc_init_ {nullptr};
    TermFn tlc_terminate_ {nullptr};
    ProcessFn tlc_process_ {nullptr};
#if TRDP_HAS_NATIVE_API
    OpenSessionFn tlc_openSession_ {nullptr};
    CloseSessionFn tlc_closeSession_ {nullptr};
    PdPublishFn tlp_publish_ {nullptr};
    PdSubscribeFn tlp_subscribe_ {nullptr};
    PdSendFn tlp_put_ {nullptr};
    MdSendFn tlm_notify_ {nullptr};
    MdSubscribeFn tlm_addListener_ {nullptr};
#endif
#endif
    NativeSessionHandle native_session_ {nullptr};
#if TRDP_HAS_NATIVE_API
    TRDP_PD_CONFIG_T pd_config_ {};
    TRDP_MD_CONFIG_T md_config_ {};
    TRDP_PROCESS_CONFIG_T process_config_ {};
#endif
    bool native_available_ {false};
    bool ready_ {false};
};

TrdpEngine::TrdpEngine(db::Database *database) : database_(database) {}

TrdpEngine::~TrdpEngine() {
    stop();
    std::lock_guard<std::mutex> lock(engine_mutex_);
    teardownStackLocked();
}

bool TrdpEngine::loadConfiguration(const config::TrdpConfig &config, const network::NetworkConfig &net_cfg) {
    std::lock_guard<std::mutex> lock(engine_mutex_);
    if (running_) {
        stopWorker();
        running_ = false;
    }
    teardownStackLocked();
    loaded_config_ = config;
    network_config_ = net_cfg;
    rebuildStateFromConfig(config.xml_content);
    if (!stack_adapter_) {
        stack_adapter_ = std::make_unique<TrdpStackAdapter>(*this);
    }
    stack_ready_ = initializeStackLocked(net_cfg);
    return stack_ready_.load();
}

void TrdpEngine::start() {
    std::lock_guard<std::mutex> lock(engine_mutex_);
    if (running_) {
        return;
    }
    if (!loaded_config_.has_value() || !network_config_.has_value()) {
        throw std::runtime_error("TRDP configuration not loaded");
    }
    if (!stack_adapter_) {
        stack_adapter_ = std::make_unique<TrdpStackAdapter>(*this);
    }
    if (!stack_ready_.load()) {
        stack_ready_ = initializeStackLocked(*network_config_);
    }
    if (!stack_ready_.load()) {
        throw std::runtime_error("Failed to initialize TRDP stack");
    }
    running_ = true;
    stop_worker_ = false;
    ensureWorker();
}

void TrdpEngine::stop() {
    std::unique_lock<std::mutex> lock(engine_mutex_);
    if (!running_) {
        return;
    }
    running_ = false;
    stop_worker_ = true;
    lock.unlock();
    stopWorker();
    lock.lock();
    teardownStackLocked();
    stack_ready_ = false;
}

std::vector<PdMessage> TrdpEngine::listOutgoingPd() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return outgoing_pd_;
}

std::vector<PdMessage> TrdpEngine::listIncomingPd() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return incoming_pd_;
}

void TrdpEngine::updateOutgoingPdPayload(int msg_id, const std::vector<uint8_t> &payload) {
    std::shared_ptr<PdRuntimeState> runtime;
    std::string src_ip;
    std::string dst_ip;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        auto idx = outgoing_pd_index_.find(msg_id);
        if (idx == outgoing_pd_index_.end()) {
            throw std::runtime_error("PD message not found");
        }
        auto &msg = outgoing_pd_[idx->second];
        msg.payload = payload;
        msg.timestamp = nowIso8601();
        auto runtime_it = pd_runtime_.find(msg_id);
        if (runtime_it == pd_runtime_.end()) {
            throw std::runtime_error("Runtime PD state missing");
        }
        runtime = runtime_it->second;
        runtime->payload = payload;
        runtime->next_cycle = std::chrono::steady_clock::now();
        src_ip = extractIp(runtime->source);
        dst_ip = extractIp(runtime->destination);
    }
    if (stack_ready_.load() && stack_adapter_ && runtime) {
        stack_adapter_->sendPd(*runtime, payload);
    }
    if (!src_ip.empty() || !dst_ip.empty()) {
        logTrdpEvent("OUT", "PD", msg_id, src_ip, dst_ip, payload);
    }
}

std::vector<MdMessage> TrdpEngine::listOutgoingMd() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return outgoing_md_;
}

std::vector<MdMessage> TrdpEngine::listIncomingMd() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return incoming_md_;
}

MdMessage TrdpEngine::sendMdMessage(const std::string &destination, const std::vector<uint8_t> &payload) {
    return sendMdMessage(destination, 0, payload);
}

MdMessage TrdpEngine::sendMdMessage(const std::string &destination, int msg_id,
                                    const std::vector<uint8_t> &payload) {
    if (!network_config_.has_value()) {
        throw std::runtime_error("Network configuration not loaded");
    }
    MdMessage message;
    std::shared_ptr<MdRuntimeState> runtime;
    bool requires_registration = false;
    auto target = sanitizeEndpoint(destination);
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        for (auto &entry : md_runtime_) {
            if (entry.second && sanitizeEndpoint(entry.second->destination) == target) {
                runtime = entry.second;
                break;
            }
        }
        if (!runtime) {
            runtime = std::make_shared<MdRuntimeState>();
            runtime->engine = this;
            runtime->runtime_id = next_md_runtime_id_++;
            runtime->name = "runtime-" + std::to_string(runtime->runtime_id);
            runtime->destination = target;
            runtime->source = sanitizeEndpoint(network_config_->local_ip + ":" +
                                               std::to_string(network_config_->md_port));
            md_runtime_[runtime->runtime_id] = runtime;
            requires_registration = true;
        }
        runtime->last_payload = payload;
        message.id = next_md_id_++;
        if (msg_id <= 0) {
            msg_id = next_md_msg_id_++;
        }
        message.msg_id = msg_id;
        runtime->last_message_id = message.msg_id;
        message.source = runtime->source;
        message.destination = runtime->destination;
        message.payload = payload;
        message.timestamp = nowIso8601();
        outgoing_md_index_[message.id] = outgoing_md_.size();
        outgoing_md_.push_back(message);
    }
    if (!runtime) {
        throw std::runtime_error("Failed to allocate MD runtime state");
    }
    if (requires_registration && stack_ready_.load() && stack_adapter_) {
        stack_adapter_->registerMdEndpoint(*runtime);
    }
    if (stack_ready_.load() && stack_adapter_) {
        stack_adapter_->sendMd(*runtime, payload, message.msg_id);
    }
    logTrdpEvent("OUT", "MD", message.msg_id, extractIp(runtime->source), extractIp(runtime->destination),
                 payload);
    return message;
}

bool TrdpEngine::initializeStackLocked(const network::NetworkConfig &net_cfg) {
    if (!stack_adapter_) {
        stack_adapter_ = std::make_unique<TrdpStackAdapter>(*this);
    }
    if (!stack_adapter_->initialize(net_cfg)) {
        return false;
    }
    for (auto &entry : pd_runtime_) {
        auto &state = *entry.second;
        if (state.is_outgoing) {
            stack_adapter_->registerPublisher(state);
        } else {
            stack_adapter_->registerSubscriber(state);
        }
    }
    for (auto &entry : md_runtime_) {
        stack_adapter_->registerMdEndpoint(*entry.second);
    }
    return stack_adapter_->ready();
}

void TrdpEngine::teardownStackLocked() {
    if (stack_adapter_) {
        stack_adapter_->shutdown();
    }
    stack_ready_ = false;
}

void TrdpEngine::rebuildStateFromConfig(const std::string &xml_content) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    clearAllStateLocked();
    const auto pd_elements = extractElements(xml_content, "pd");
    for (const auto &element : pd_elements) {
        PdMessage message;
        message.id = next_pd_id_++;
        auto it_name = element.attributes.find("name");
        message.name = it_name != element.attributes.end() ? it_name->second : "PD-" + std::to_string(message.id);
        auto it_cycle = element.attributes.find("cycle");
        message.cycle_time_ms = it_cycle != element.attributes.end() ? safeStoi(it_cycle->second) : 0;
        std::string payload_str;
        if (auto attr_payload = element.attributes.find("payload"); attr_payload != element.attributes.end()) {
            payload_str = attr_payload->second;
        } else {
            payload_str = element.body;
        }
        message.payload = parseHexPayload(payload_str);
        message.timestamp = nowIso8601();
        std::string direction = "outgoing";
        if (auto dir_attr = element.attributes.find("direction"); dir_attr != element.attributes.end()) {
            direction = toLowerCopy(dir_attr->second);
        }
        bool is_outgoing = direction != "in" && direction != "incoming" && direction != "subscriber";
        auto runtime = std::make_shared<PdRuntimeState>();
        runtime->engine = this;
        runtime->id = message.id;
        runtime->name = message.name;
        runtime->is_outgoing = is_outgoing;
        runtime->cycle_ms = message.cycle_time_ms;
        runtime->payload = message.payload;
        runtime->next_cycle = std::chrono::steady_clock::now();
        if (auto dst = element.attributes.find("destination"); dst != element.attributes.end()) {
            runtime->destination = sanitizeEndpoint(dst->second);
        }
        if (runtime->destination.empty() && network_config_) {
            runtime->destination = network_config_->local_ip + ":" + std::to_string(network_config_->pd_port);
        }
        if (auto src = element.attributes.find("source"); src != element.attributes.end()) {
            runtime->source = sanitizeEndpoint(src->second);
        }
        if (runtime->source.empty() && network_config_) {
            runtime->source = network_config_->local_ip + ":" + std::to_string(network_config_->pd_port);
        }
        pd_runtime_[message.id] = runtime;
        if (is_outgoing) {
            outgoing_pd_index_[message.id] = outgoing_pd_.size();
            outgoing_pd_.push_back(message);
        } else {
            incoming_pd_index_[message.id] = incoming_pd_.size();
            incoming_pd_.push_back(message);
        }
    }
    const auto md_elements = extractElements(xml_content, "md");
    for (const auto &element : md_elements) {
        auto runtime = std::make_shared<MdRuntimeState>();
        runtime->engine = this;
        runtime->runtime_id = next_md_runtime_id_++;
        if (auto name_attr = element.attributes.find("name"); name_attr != element.attributes.end()) {
            runtime->name = name_attr->second;
        }
        if (auto dst = element.attributes.find("destination"); dst != element.attributes.end()) {
            runtime->destination = sanitizeEndpoint(dst->second);
        }
        if (runtime->destination.empty() && network_config_) {
            runtime->destination = network_config_->local_ip + ":" + std::to_string(network_config_->md_port);
        }
        if (auto src = element.attributes.find("source"); src != element.attributes.end()) {
            runtime->source = sanitizeEndpoint(src->second);
        }
        if (runtime->source.empty() && network_config_) {
            runtime->source = network_config_->local_ip + ":" + std::to_string(network_config_->md_port);
        }
        md_runtime_[runtime->runtime_id] = runtime;
    }
}

void TrdpEngine::runEventLoop() {
    while (!stop_worker_.load()) {
        std::vector<std::shared_ptr<PdRuntimeState>> due;
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            const auto now = std::chrono::steady_clock::now();
            for (auto &entry : pd_runtime_) {
                auto &state = *entry.second;
                if (!state.is_outgoing || state.cycle_ms <= 0) {
                    continue;
                }
                if (now >= state.next_cycle) {
                    due.push_back(entry.second);
                    scheduleNextCycle(state);
                }
            }
        }
        for (const auto &state_ptr : due) {
            if (!stack_ready_.load() || !stack_adapter_) {
                continue;
            }
            stack_adapter_->sendPd(*state_ptr, state_ptr->payload);
            logTrdpEvent("OUT", "PD", state_ptr->id, extractIp(state_ptr->source),
                         extractIp(state_ptr->destination), state_ptr->payload);
            std::lock_guard<std::mutex> lock(state_mutex_);
            auto idx = outgoing_pd_index_.find(state_ptr->id);
            if (idx != outgoing_pd_index_.end()) {
                outgoing_pd_[idx->second].timestamp = nowIso8601();
            }
        }
        if (stack_ready_.load() && stack_adapter_) {
            stack_adapter_->iterate();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void TrdpEngine::scheduleNextCycle(PdRuntimeState &state) {
    if (state.cycle_ms <= 0) {
        state.next_cycle = std::chrono::steady_clock::now() + std::chrono::hours(24);
        return;
    }
    state.next_cycle = std::chrono::steady_clock::now() + std::chrono::milliseconds(state.cycle_ms);
}

void TrdpEngine::handleIncomingPd(int msg_id, const std::vector<uint8_t> &payload, const std::string &src_ip,
                                  const std::string &dst_ip) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    auto idx = incoming_pd_index_.find(msg_id);
    if (idx == incoming_pd_index_.end()) {
        PdMessage msg;
        msg.id = msg_id;
        msg.name = "PD-" + std::to_string(msg_id);
        msg.payload = payload;
        msg.timestamp = nowIso8601();
        incoming_pd_index_[msg_id] = incoming_pd_.size();
        incoming_pd_.push_back(msg);
    } else {
        auto &msg = incoming_pd_[idx->second];
        msg.payload = payload;
        msg.timestamp = nowIso8601();
    }
    logTrdpEvent("IN", "PD", msg_id, src_ip, dst_ip, payload);
}

void TrdpEngine::handleIncomingMd(int msg_id, const std::vector<uint8_t> &payload, const std::string &src_ip,
                                  const std::string &dst_ip) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    MdMessage message;
    message.id = next_md_id_++;
    if (msg_id <= 0) {
        msg_id = next_md_msg_id_++;
    }
    message.msg_id = msg_id;
    message.source = src_ip;
    message.destination = dst_ip;
    message.payload = payload;
    message.timestamp = nowIso8601();
    incoming_md_index_[message.id] = incoming_md_.size();
    incoming_md_.push_back(message);
    logTrdpEvent("IN", "MD", message.msg_id, src_ip, dst_ip, payload);
}

void TrdpEngine::logTrdpEvent(const std::string &direction, const std::string &type, int msg_id,
                              const std::string &src_ip, const std::string &dst_ip,
                              const std::vector<uint8_t> &payload) {
    if (database_ == nullptr) {
        return;
    }
    auto *db = database_->handle();
    if (db == nullptr) {
        return;
    }
    sqlite3_stmt *stmt = nullptr;
    const char *sql =
        "INSERT INTO trdp_logs (direction, type, msg_id, src_ip, dst_ip, payload) VALUES (?, ?, ?, ?, ?, ?);";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return;
    }
    sqlite3_bind_text(stmt, 1, direction.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, msg_id);
    sqlite3_bind_text(stmt, 4, src_ip.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, dst_ip.c_str(), -1, SQLITE_TRANSIENT);
    if (!payload.empty()) {
        sqlite3_bind_blob(stmt, 6, payload.data(), static_cast<int>(payload.size()), SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(stmt, 6);
    }
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

std::string TrdpEngine::sanitizeEndpoint(const std::string &endpoint) {
    return trimCopy(endpoint);
}

std::string TrdpEngine::extractIp(const std::string &endpoint) {
    auto cleaned = trimCopy(endpoint);
    auto pos = cleaned.find(':');
    if (pos == std::string::npos) {
        return cleaned;
    }
    return cleaned.substr(0, pos);
}

uint16_t TrdpEngine::extractPort(const std::string &endpoint, uint16_t fallback) {
    auto cleaned = trimCopy(endpoint);
    auto pos = cleaned.find(':');
    if (pos == std::string::npos) {
        return fallback;
    }
    auto port_str = cleaned.substr(pos + 1);
    if (port_str.empty()) {
        return fallback;
    }
    return static_cast<uint16_t>(safeStoi(port_str, fallback));
}

void TrdpEngine::ensureWorker() {
    if (worker_thread_.joinable()) {
        return;
    }
    worker_thread_ = std::thread(&TrdpEngine::runEventLoop, this);
}

void TrdpEngine::stopWorker() {
    stop_worker_ = true;
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
}

void TrdpEngine::clearAllStateLocked() {
    outgoing_pd_.clear();
    incoming_pd_.clear();
    outgoing_md_.clear();
    incoming_md_.clear();
    outgoing_pd_index_.clear();
    incoming_pd_index_.clear();
    outgoing_md_index_.clear();
    incoming_md_index_.clear();
    pd_runtime_.clear();
    md_runtime_.clear();
    next_pd_id_ = 1;
    next_md_id_ = 1;
    next_md_msg_id_ = 1;
    next_md_runtime_id_ = 1;
}

void TrdpEngine::pdCallbackBridge(void *ref_con, const uint8_t *payload, uint32_t size, const char *src_ip,
                                  const char *dst_ip) {
    if (ref_con == nullptr) {
        return;
    }
    auto *state = static_cast<PdRuntimeState *>(ref_con);
    std::vector<uint8_t> buffer;
    if (payload != nullptr && size > 0U) {
        buffer.assign(payload, payload + size);
    }
    std::string src = src_ip != nullptr ? src_ip : "";
    std::string dst = dst_ip != nullptr ? dst_ip : "";
    state->engine->handleIncomingPd(state->id, buffer, src, dst);
}

void TrdpEngine::mdCallbackBridge(void *ref_con, const uint8_t *payload, uint32_t size, const char *src_ip,
                                  const char *dst_ip) {
    if (ref_con == nullptr) {
        return;
    }
    auto *state = static_cast<MdRuntimeState *>(ref_con);
    std::vector<uint8_t> buffer;
    if (payload != nullptr && size > 0U) {
        buffer.assign(payload, payload + size);
    }
    std::string src = src_ip != nullptr ? src_ip : "";
    std::string dst = dst_ip != nullptr ? dst_ip : "";
    state->engine->handleIncomingMd(state->last_message_id, buffer, src, dst);
}

std::string TrdpEngine::nowIso8601() {
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
#ifdef _WIN32
    gmtime_s(&tm, &tt);
#else
    gmtime_r(&tt, &tm);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buffer;
}

}  // namespace trdp::stack
