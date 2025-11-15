#include "trdp/TrdpXmlParser.hpp"

#include <array>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include <trdp/tau_xml.h>

#include "trdp/XmlUtils.hpp"

namespace trdp::config {
namespace {

template <typename F>
class ScopeExit {
public:
    explicit ScopeExit(F &&func) : func_(std::forward<F>(func)), active_(true) {}
    ScopeExit(const ScopeExit &) = delete;
    ScopeExit &operator=(const ScopeExit &) = delete;
    ScopeExit(ScopeExit &&other) noexcept : func_(std::move(other.func_)), active_(other.active_) {
        other.active_ = false;
    }
    ~ScopeExit() {
        if (active_) {
            func_();
        }
    }
    void release() { active_ = false; }

private:
    F func_;
    bool active_;
};

std::string describeTrdpError(TRDP_ERR_T error) {
    return "TRDP error " + std::to_string(static_cast<int>(error));
}

std::string cStringOrEmpty(const CHAR8 *value) {
    if (value == nullptr) {
        return {};
    }
    return std::string(reinterpret_cast<const char *>(value));
}

template <size_t N>
std::string cStringOrEmpty(const CHAR8 (*value)[N]) {
    if (value == nullptr) {
        return {};
    }
    return cStringOrEmpty(&(*value)[0]);
}

std::string firstUserLabel(const TRDP_EXCHG_PAR_T &entry) {
    if (entry.pDest != nullptr && entry.destCnt > 0U && entry.pDest[0].pUriUser != nullptr) {
        return cStringOrEmpty(entry.pDest[0].pUriUser);
    }
    if (entry.pSrc != nullptr && entry.srcCnt > 0U && entry.pSrc[0].pUriUser != nullptr) {
        return cStringOrEmpty(entry.pSrc[0].pUriUser);
    }
    return {};
}

std::string joinHosts(const TRDP_DEST_T *dest, UINT32 count) {
    if (dest == nullptr || count == 0U) {
        return {};
    }
    std::string combined;
    for (UINT32 i = 0; i < count; ++i) {
        const auto &entry = dest[i];
        auto host = cStringOrEmpty(entry.pUriHost);
        if (host.empty()) {
            continue;
        }
        if (!combined.empty()) {
            combined.append(", ");
        }
        combined.append(host);
    }
    return combined;
}

std::string joinSourceHosts(const TRDP_SRC_T *src, UINT32 count) {
    if (src == nullptr || count == 0U) {
        return {};
    }
    std::string combined;
    for (UINT32 i = 0; i < count; ++i) {
        const auto &entry = src[i];
        auto host = cStringOrEmpty(entry.pUriHost1);
        if (host.empty()) {
            continue;
        }
        if (!combined.empty()) {
            combined.append(", ");
        }
        combined.append(host);
    }
    return combined;
}

int microsecondsToMs(UINT32 value) {
    if (value == 0U) {
        return 0;
    }
    return static_cast<int>(value / 1000U);
}

TrdpTelegramType determineTelegramType(const TRDP_EXCHG_PAR_T &entry) {
    if (entry.pMdPar != nullptr) {
        return TrdpTelegramType::kMd;
    }
    return TrdpTelegramType::kPd;
}

TrdpTelegramDirection pdDirectionFromOption(TRDP_EXCHG_OPTION_T option) {
    switch (option) {
        case TRDP_EXCHG_SOURCE:
            return TrdpTelegramDirection::kPublisher;
        case TRDP_EXCHG_SINK:
            return TrdpTelegramDirection::kSubscriber;
        case TRDP_EXCHG_SOURCESINK:
            return TrdpTelegramDirection::kPublisher;
        default:
            return TrdpTelegramDirection::kPublisher;
    }
}

TrdpTelegramDirection mdDirectionFromOption(TRDP_EXCHG_OPTION_T option) {
    switch (option) {
        case TRDP_EXCHG_SOURCE:
            return TrdpTelegramDirection::kResponder;
        case TRDP_EXCHG_SINK:
            return TrdpTelegramDirection::kListener;
        case TRDP_EXCHG_SOURCESINK:
            return TrdpTelegramDirection::kResponder;
        default:
            return TrdpTelegramDirection::kResponder;
    }
}

TrdpTelegramDefinition buildBaseDefinition(const TRDP_EXCHG_PAR_T &entry) {
    TrdpTelegramDefinition definition;
    definition.name = firstUserLabel(entry);
    definition.com_id = static_cast<int>(entry.comId);
    if (entry.datasetId != 0U) {
        definition.dataset = std::to_string(entry.datasetId);
    }
    if (entry.pPdPar != nullptr) {
        definition.cycle_time_ms = microsecondsToMs(entry.pPdPar->cycle);
        definition.timeout_ms = microsecondsToMs(entry.pPdPar->timeout);
    } else if (entry.pMdPar != nullptr) {
        definition.timeout_ms = microsecondsToMs(entry.pMdPar->replyTimeout);
    }
    return definition;
}

void populateDirectionSpecificFields(const TRDP_EXCHG_PAR_T &entry, TrdpTelegramDefinition &definition,
                                     TrdpTelegramDirection direction) {
    if (entry.pPdPar != nullptr) {
        definition.type = TrdpTelegramType::kPd;
        definition.destination = joinHosts(entry.pDest, entry.destCnt);
        definition.source = joinSourceHosts(entry.pSrc, entry.srcCnt);
        if (entry.type == TRDP_EXCHG_SOURCESINK && direction == TrdpTelegramDirection::kSubscriber) {
            definition.direction = TrdpTelegramDirection::kSubscriber;
        } else {
            definition.direction = direction;
        }
    } else {
        definition.type = TrdpTelegramType::kMd;
        definition.destination = joinHosts(entry.pDest, entry.destCnt);
        definition.source = joinSourceHosts(entry.pSrc, entry.srcCnt);
        definition.direction = direction;
    }
}

std::vector<TrdpTelegramDefinition> convertExchangeToTelegrams(const TRDP_EXCHG_PAR_T &entry) {
    std::vector<TrdpTelegramDefinition> telegrams;
    const bool hasPublishSide = entry.destCnt > 0U &&
                                (entry.type == TRDP_EXCHG_SOURCE || entry.type == TRDP_EXCHG_SOURCESINK);
    const bool hasSubscribeSide = entry.srcCnt > 0U &&
                                  (entry.type == TRDP_EXCHG_SINK || entry.type == TRDP_EXCHG_SOURCESINK);
    if (hasPublishSide) {
        auto definition = buildBaseDefinition(entry);
        if (entry.pMdPar != nullptr) {
            populateDirectionSpecificFields(entry, definition, mdDirectionFromOption(TRDP_EXCHG_SOURCE));
        } else {
            populateDirectionSpecificFields(entry, definition, pdDirectionFromOption(TRDP_EXCHG_SOURCE));
        }
        telegrams.push_back(std::move(definition));
    }
    if (hasSubscribeSide) {
        auto definition = buildBaseDefinition(entry);
        if (entry.pMdPar != nullptr) {
            populateDirectionSpecificFields(entry, definition, mdDirectionFromOption(TRDP_EXCHG_SINK));
        } else {
            populateDirectionSpecificFields(entry, definition, pdDirectionFromOption(TRDP_EXCHG_SINK));
        }
        telegrams.push_back(std::move(definition));
    }
    if (telegrams.empty()) {
        auto definition = buildBaseDefinition(entry);
        definition.type = determineTelegramType(entry);
        if (definition.type == TrdpTelegramType::kMd) {
            definition.direction = mdDirectionFromOption(entry.type);
        } else {
            definition.direction = pdDirectionFromOption(entry.type);
        }
        definition.destination = joinHosts(entry.pDest, entry.destCnt);
        definition.source = joinSourceHosts(entry.pSrc, entry.srcCnt);
        telegrams.push_back(std::move(definition));
    }
    return telegrams;
}

bool hasTrdpMarkers(const std::string &xml_content) {
    const auto lowered = xml::toLowerCopy(xml_content);
    if (lowered.find("trdp-config.xsd") != std::string::npos) {
        return true;
    }
    static const std::array<const char *, 8> markers = {"<device ",          "<device-configuration", "<mem-block-list",
                                                        "<bus-interface-list", "<bus-interface",        "<pd-com-parameter",
                                                        "<md-com-parameter",  "<telegram"};
    for (auto marker : markers) {
        if (lowered.find(marker) != std::string::npos) {
            return true;
        }
    }
    return false;
}

}  // namespace

std::optional<TrdpXmlConfig> parseTrdpXmlConfig(const std::string &xml_content, std::string *error_out) {
    if (xml_content.empty()) {
        if (error_out != nullptr) {
            *error_out = "XML content is empty";
        }
        return std::nullopt;
    }

    std::vector<char> xml_buffer(xml_content.begin(), xml_content.end());
    xml_buffer.push_back('\0');

    TRDP_XML_DOC_HANDLE_T doc_handle {};
    TRDP_ERR_T err = tau_prepareXmlMem(xml_buffer.data(), xml_buffer.size(), &doc_handle);
    if (err != TRDP_NO_ERR) {
        if (error_out != nullptr) {
            *error_out = "tau_prepareXmlMem failed: " + describeTrdpError(err);
        }
        return std::nullopt;
    }

    ScopeExit doc_guard([&]() { tau_freeXmlDoc(&doc_handle); });

    TRDP_MEM_CONFIG_T mem_config {};
    TRDP_DBG_CONFIG_T dbg_config {};
    UINT32 num_com_params = 0;
    TRDP_COM_PAR_T *com_params = nullptr;
    UINT32 num_interfaces = 0;
    TRDP_IF_CONFIG_T *if_configs = nullptr;

    err = tau_readXmlDeviceConfig(&doc_handle, &mem_config, &dbg_config, &num_com_params, &com_params, &num_interfaces,
                                  &if_configs);
    ScopeExit device_guard([&]() {
        if (com_params != nullptr) {
            std::free(com_params);
        }
        if (if_configs != nullptr) {
            std::free(if_configs);
        }
    });
    if (err != TRDP_NO_ERR) {
        if (error_out != nullptr) {
            *error_out = "tau_readXmlDeviceConfig failed: " + describeTrdpError(err);
        }
        return std::nullopt;
    }
    if (num_interfaces == 0U || if_configs == nullptr) {
        if (error_out != nullptr) {
            *error_out = "XML does not declare any <bus-interface> elements";
        }
        return std::nullopt;
    }

    TrdpXmlConfig config;
    for (UINT32 i = 0; i < num_interfaces; ++i) {
        const auto &iface_cfg = if_configs[i];
        TrdpInterfaceDefinition iface;
        iface.name = cStringOrEmpty(iface_cfg.ifName);

        UINT32 num_exchange = 0;
        TRDP_EXCHG_PAR_T *exchange_params = nullptr;
        TRDP_PROCESS_CONFIG_T process_config {};
        TRDP_PD_CONFIG_T pd_config {};
        TRDP_MD_CONFIG_T md_config {};

        err = tau_readXmlInterfaceConfig(&doc_handle, iface_cfg.ifName, &process_config, &pd_config, &md_config,
                                         &num_exchange, &exchange_params);
        ScopeExit telegram_guard([&]() {
            if (exchange_params != nullptr) {
                tau_freeTelegrams(num_exchange, exchange_params);
            }
        });
        if (err != TRDP_NO_ERR) {
            if (error_out != nullptr) {
                *error_out = "tau_readXmlInterfaceConfig failed for interface '" + iface.name + "': " +
                             describeTrdpError(err);
            }
            return std::nullopt;
        }

        for (UINT32 j = 0; j < num_exchange; ++j) {
            const auto &exchange = exchange_params[j];
            auto defs = convertExchangeToTelegrams(exchange);
            iface.telegrams.insert(iface.telegrams.end(), defs.begin(), defs.end());
        }

        if (!iface.telegrams.empty()) {
            config.interfaces.push_back(std::move(iface));
        }
    }

    if (config.interfaces.empty()) {
        if (error_out != nullptr) {
            *error_out = "No telegram definitions were found inside <bus-interface> sections";
        }
        return std::nullopt;
    }

    return config;
}

bool looksLikeTrdpXml(const std::string &xml_content) {
    return hasTrdpMarkers(xml_content);
}

}  // namespace trdp::config
