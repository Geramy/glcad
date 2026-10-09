#pragma once

#include <vector>

#include "geometry/body.hpp"
#include "ui/modeller.hpp"
#include "math/config.hpp"

namespace gcapp {

inline constexpr gcm::u16 kSolidsFrame = 0x5344;
inline constexpr gcm::u16 kTextsFrame = 0x5458;
inline constexpr gcm::u16 kFeaturesFrame = 0x4645;
inline constexpr gcm::u16 kLayersFrame = 0x4C59;
inline constexpr gcm::u16 kLayoutsFrame = 0x4C4F;
inline constexpr gcm::u16 kPaperContentFrame = 0x5043;
inline constexpr gcm::u16 kModelNotesFrame = 0x4D4E;
inline constexpr gcm::u16 kMetadataFrame = 0x4D44;

std::vector<gcm::u8> encode_solids(const std::vector<gtopo::Body>& bodies);
bool decode_solids(const std::vector<gcm::u8>& bytes, std::vector<gtopo::Body>& out);

std::vector<std::vector<Modeller::Feature>> decode_feature_history(
    const std::vector<gcm::u8>& bytes, std::size_t body_count);
std::string encode_feature_history(
    const std::vector<std::vector<Modeller::Feature>>& hist);

}
