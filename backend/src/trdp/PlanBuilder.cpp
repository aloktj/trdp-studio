#include "trdp/PlanBuilder.hpp"

#include <cstddef>
#include <set>
#include <sstream>

namespace trdp::config {
namespace {

struct PlanMetrics {
    std::size_t interface_count {0};
    std::size_t telegram_count {0};
    std::size_t pd_count {0};
    std::size_t md_count {0};
    std::size_t pd_publishers {0};
    std::size_t pd_subscribers {0};
    std::size_t md_publishers {0};
    std::size_t md_subscribers {0};
    std::set<int> com_ids;
    std::set<std::string> dataset_ids;
    std::vector<std::string> interface_names;
};

bool isPublisherDirection(TrdpTelegramDirection direction) {
    return direction == TrdpTelegramDirection::kPublisher || direction == TrdpTelegramDirection::kResponder;
}

bool isSubscriberDirection(TrdpTelegramDirection direction) {
    return direction == TrdpTelegramDirection::kSubscriber || direction == TrdpTelegramDirection::kListener;
}

PlanMetrics collectMetrics(const TrdpXmlConfig &config) {
    PlanMetrics metrics;
    metrics.interface_count = config.interfaces.size();
    metrics.interface_names.reserve(config.interfaces.size());
    for (std::size_t i = 0; i < config.interfaces.size(); ++i) {
        const auto &iface = config.interfaces[i];
        std::string name = iface.name.empty() ? "bus-interface-" + std::to_string(i + 1) : iface.name;
        metrics.interface_names.push_back(name);
        for (const auto &telegram : iface.telegrams) {
            metrics.telegram_count++;
            if (telegram.com_id > 0) {
                metrics.com_ids.insert(telegram.com_id);
            }
            if (!telegram.dataset.empty()) {
                metrics.dataset_ids.insert(telegram.dataset);
            }
            if (telegram.type == TrdpTelegramType::kPd) {
                metrics.pd_count++;
                if (isPublisherDirection(telegram.direction)) {
                    metrics.pd_publishers++;
                }
                if (isSubscriberDirection(telegram.direction)) {
                    metrics.pd_subscribers++;
                }
            } else {
                metrics.md_count++;
                if (isPublisherDirection(telegram.direction)) {
                    metrics.md_publishers++;
                }
                if (isSubscriberDirection(telegram.direction)) {
                    metrics.md_subscribers++;
                }
            }
        }
    }
    return metrics;
}

std::string pluralize(std::size_t count, const std::string &singular, const std::string &plural) {
    return count == 1 ? singular : plural;
}

std::string commaList(const std::vector<std::string> &values) {
    std::ostringstream oss;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (values[i].empty()) {
            continue;
        }
        if (oss.tellp() > 0) {
            oss << ", ";
        }
        oss << values[i];
    }
    return oss.str();
}

TrdpPlanSection makeXmlSection(const PlanMetrics &metrics) {
    TrdpPlanSection section;
    section.name = "XML parsing & validation";

    TrdpPlanStep load;
    load.title = "Load XML document";
    {
        std::ostringstream desc;
        desc << "Call tau_prepareXmlDoc() once at startup to parse the selected configuration ("
             << metrics.interface_count << " "
             << pluralize(metrics.interface_count, "interface", "interfaces")
             << ") into a DOM that can be reused by every downstream parser.";
        load.description = desc.str();
    }
    load.api_calls = {"tau_prepareXmlDoc"};
    section.steps.push_back(load);

    TrdpPlanStep device;
    device.title = "Extract device defaults";
    {
        std::ostringstream desc;
        desc << "Immediately invoke tau_readXmlDeviceConfig() to obtain memory pools, debug config, and "
             << (metrics.com_ids.empty() ? std::string("the COM parameter table")
                                        : std::to_string(metrics.com_ids.size()) + " COM " +
                                              pluralize(metrics.com_ids.size(), "ID", "IDs"))
             << " so the same data can be passed to tlc_init()/tlc_openSession().";
        device.description = desc.str();
    }
    device.api_calls = {"tau_readXmlDeviceConfig"};
    section.steps.push_back(device);

    TrdpPlanStep datasets;
    datasets.title = "Prime dataset mappings";
    {
        std::ostringstream desc;
        if (metrics.dataset_ids.empty()) {
            desc << "Call tau_readXmlDatasetConfig() even if the XML does not reference datasets so the marshalling context "
                    "stays synchronized with the schema definitions.";
        } else {
            desc << "Use tau_readXmlDatasetConfig() to load " << metrics.dataset_ids.size() << " dataset "
                 << pluralize(metrics.dataset_ids.size(), "definition", "definitions")
                 << " and the ComID↔Dataset map before setting up the marshaller.";
        }
        datasets.description = desc.str();
    }
    datasets.api_calls = {"tau_readXmlDatasetConfig"};
    section.steps.push_back(datasets);

    TrdpPlanStep interfaces;
    interfaces.title = "Collect interface telegrams";
    {
        std::ostringstream desc;
        desc << "Iterate every <bus-interface> (" << metrics.interface_count << ") with tau_readXmlInterfaceConfig() to "
             << "retrieve PD/MD defaults plus " << metrics.telegram_count << " telegram "
             << pluralize(metrics.telegram_count, "entry", "entries")
             << "; release the exchange arrays via tau_freeTelegrams() once the runtime plan is built.";
        const auto joined = commaList(metrics.interface_names);
        if (!joined.empty()) {
            desc << " Interfaces: " << joined << ".";
        }
        interfaces.description = desc.str();
    }
    interfaces.api_calls = {"tau_readXmlInterfaceConfig", "tau_freeTelegrams"};
    section.steps.push_back(interfaces);

    return section;
}

TrdpPlanSection makeInitSection(const PlanMetrics &metrics) {
    TrdpPlanSection section;
    section.name = "TRDP stack initialization & marshalling";

    TrdpPlanStep init;
    init.title = "Initialize TRDP";
    {
        std::ostringstream desc;
        desc << "Call tlc_init() once with the device-level memory/debug configuration so the stack can allocate pools for "
             << (metrics.pd_count + metrics.md_count) << " telegram "
             << pluralize(metrics.pd_count + metrics.md_count, "entry", "entries") << ".";
        init.description = desc.str();
    }
    init.api_calls = {"tlc_init"};
    section.steps.push_back(init);

    TrdpPlanStep marshall;
    marshall.title = "Create marshalling context";
    {
        std::ostringstream desc;
        desc << "Feed the ComID↔Dataset table into tau_initMarshall(), then store the returned reference and the "
             << "tau_marshall()/tau_unmarshall callbacks inside TRDP_MARSHALL_CONFIG_T so PD payloads follow the XML schema.";
        marshall.description = desc.str();
    }
    marshall.api_calls = {"tau_initMarshall", "tau_marshall", "tau_unmarshall"};
    section.steps.push_back(marshall);

    return section;
}

TrdpPlanSection makeSessionSection(const PlanMetrics &metrics) {
    TrdpPlanSection section;
    section.name = "Session lifecycle";

    TrdpPlanStep open;
    open.title = "Open per-interface sessions";
    {
        std::ostringstream desc;
        desc << "Create one tlc_openSession() per interface (" << metrics.interface_count << ") using the parsed process, "
             << "PD, and MD defaults plus the marshalling config so each bus interface can manage its telegrams independently.";
        open.description = desc.str();
    }
    open.api_calls = {"tlc_openSession"};
    section.steps.push_back(open);

    TrdpPlanStep refresh;
    refresh.title = "Refresh runtime tables";
    refresh.description =
        "If the fast test style is used, call tlc_updateSession() right after registering publishers/subscribers so the stack "
        "rebuilds its lookup tables immediately.";
    refresh.api_calls = {"tlc_updateSession"};
    section.steps.push_back(refresh);

    TrdpPlanStep loop;
    loop.title = "Drive the processing loop";
    loop.description =
        "Single-threaded deployments poll tlc_process(), while split RX/TX loops rely on tlp_getInterval(), "
        "tlp_processReceive(), and tlp_processSend() to service sockets with deterministic timing.";
    loop.api_calls = {"tlc_process", "tlp_getInterval", "tlp_processReceive", "tlp_processSend"};
    section.steps.push_back(loop);

    return section;
}

TrdpPlanSection makeOrchestrationSection(const PlanMetrics &metrics) {
    TrdpPlanSection section;
    section.name = "Publisher/subscriber orchestration";

    TrdpPlanStep publishers;
    publishers.title = "Create PD publishers";
    {
        std::ostringstream desc;
        desc << "Instantiate " << metrics.pd_publishers << " PD "
             << pluralize(metrics.pd_publishers, "publisher", "publishers")
             << " with tlp_publish(), seeding each handle with the initial payload before calling tlp_put() inside the "
             << "cyclic scheduler.";
        publishers.description = desc.str();
    }
    publishers.api_calls = {"tlp_publish", "tlp_put"};
    section.steps.push_back(publishers);

    TrdpPlanStep subscribers;
    subscribers.title = "Register PD subscribers";
    {
        std::ostringstream desc;
        desc << "Attach " << metrics.pd_subscribers << " PD "
             << pluralize(metrics.pd_subscribers, "subscriber", "subscribers")
             << " with tlp_subscribe() using the XML timeout behavior, then fetch the latest payloads through tlp_get() or the "
             << "configured callbacks.";
        subscribers.description = desc.str();
    }
    subscribers.api_calls = {"tlp_subscribe", "tlp_get"};
    section.steps.push_back(subscribers);

    TrdpPlanStep md;
    md.title = "Handle MD conversations";
    {
        std::ostringstream desc;
        desc << "Use tlm_addListener() to stand up the " << metrics.md_subscribers << " MD listener"
             << (metrics.md_subscribers == 1 ? "" : "s")
             << " and tlm_notify() (or tlm_request()/tlm_reply()) for the " << metrics.md_publishers << " active MD "
             << pluralize(metrics.md_publishers, "endpoint", "endpoints") << ".";
        md.description = desc.str();
    }
    md.api_calls = {"tlm_addListener", "tlm_notify"};
    section.steps.push_back(md);

    return section;
}

TrdpPlanSection makeShutdownSection(const PlanMetrics &metrics) {
    TrdpPlanSection section;
    section.name = "Graceful shutdown";

    TrdpPlanStep teardown;
    teardown.title = "Tear down telegrams";
    {
        std::ostringstream desc;
        desc << "Call tlp_unpublish()/tlp_unsubscribe() for the " << metrics.pd_count << " PD and " << metrics.md_count
             << " MD endpoints, then close sessions via tlc_closeSession(), terminate the stack with tlc_terminate(), and "
             << "release the XML resources (tau_freeTelegrams(), tau_freeXmlDoc()).";
        teardown.description = desc.str();
    }
    teardown.api_calls = {"tlp_unpublish", "tlp_unsubscribe", "tlc_closeSession", "tlc_terminate", "tau_freeTelegrams",
                          "tau_freeXmlDoc"};
    section.steps.push_back(teardown);

    return section;
}

}  // namespace

std::vector<TrdpPlanSection> TrdpPlanBuilder::buildPlan(const TrdpXmlConfig &config) const {
    const PlanMetrics metrics = collectMetrics(config);
    std::vector<TrdpPlanSection> sections;
    sections.reserve(5);
    sections.push_back(makeXmlSection(metrics));
    sections.push_back(makeInitSection(metrics));
    sections.push_back(makeSessionSection(metrics));
    sections.push_back(makeOrchestrationSection(metrics));
    sections.push_back(makeShutdownSection(metrics));
    return sections;
}

}  // namespace trdp::config
