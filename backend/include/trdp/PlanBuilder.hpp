#pragma once

#include <string>
#include <vector>

#include "trdp/TrdpXmlParser.hpp"

namespace trdp::config {

struct TrdpPlanStep {
    std::string title;
    std::string description;
    std::vector<std::string> api_calls;
};

struct TrdpPlanSection {
    std::string name;
    std::vector<TrdpPlanStep> steps;
};

class TrdpPlanBuilder {
public:
    TrdpPlanBuilder() = default;
    std::vector<TrdpPlanSection> buildPlan(const TrdpXmlConfig &config) const;
};

}  // namespace trdp::config
