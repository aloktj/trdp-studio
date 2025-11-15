#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace trdp::xml {

struct XmlElement {
    std::unordered_map<std::string, std::string> attributes;
    std::string body;
};

std::string trimCopy(const std::string &value);
std::string toLowerCopy(std::string value);
int safeStoi(const std::string &value, int fallback = 0);
std::vector<uint8_t> parseHexPayload(const std::string &raw);
std::vector<XmlElement> extractElements(const std::string &xml, const std::string &tag);

}  // namespace trdp::xml
