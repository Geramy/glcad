#pragma once

#include <string>
#include <vector>

#include "geometry/recognise.hpp"

namespace gtopo {

enum class RuleKind : gcm::u8 {
    Coplanar,
    Concentric,
    Parallel,
    Perpendicular,
    Tangent,
    Symmetric,
    EqualRadius,
    Coaxial,
};

const char* rule_kind_name(RuleKind k) noexcept;

struct LiveRule {
    RuleKind kind{RuleKind::Coplanar};
    std::vector<FaceId> faces;
    bool enabled{true};
    f64 tolerance{0.0};
    Plane plane{};

    bool involves(FaceId f) const noexcept;
};

struct LiveRuleSettings {
    f64 angle_tolerance{0.01};      // radians
    f64 distance_tolerance{1e-6};   // metres
    bool coplanar{true};
    bool concentric{true};
    bool parallel{true};
    bool perpendicular{true};
    bool tangent{true};
    bool symmetric{true};
    bool equal_radius{true};
    std::size_t max_rules{512};
};

class LiveRules {
public:
    void discover(const Body& b, const Volume& v,
                  const LiveRuleSettings& s = {});

    std::size_t size() const noexcept { return rules_.size(); }
    const std::vector<LiveRule>& all() const noexcept { return rules_; }
    LiveRule* at(std::size_t i) {
        return i < rules_.size() ? &rules_[i] : nullptr;
    }

    void clear() { rules_.clear(); }
    void set_enabled(RuleKind k, bool on);
    void set_enabled(std::size_t index, bool on);

    void suspend() noexcept { suspended_ = true; }
    void resume() noexcept { suspended_ = false; }
    bool suspended() const noexcept { return suspended_; }

    struct Companion {
        FaceId   face{kInvalid};
        RuleKind because{RuleKind::Coplanar};
        f64 factor{1.0};
    };
    std::vector<Companion> companions(FaceId face) const;

    std::vector<std::size_t> rules_for(FaceId face) const;

private:
    std::vector<LiveRule> rules_;
    bool                  suspended_{false};
};

enum class SelectScope : gcm::u8 {
    Face,
    Feature,
    Tangent,
    Coplanar,
    SameRadius,
    Body,
    Loop,
};

const char* select_scope_name(SelectScope s) noexcept;

struct SelectionResult {
    std::vector<FaceId> faces;
    SelectScope         scope{SelectScope::Face};
    std::string         description;
    bool ok{false};
};

class SelectManager {
public:
    void set_body(const Body* b, const Volume* v);
    void set_features(const RecognitionResult* r) { features_ = r; }

    SelectionResult select(FaceId picked, SelectScope scope) const;

    SelectScope next_scope(SelectScope current) const;

    f64& tangent_tolerance() noexcept { return tangent_tolerance_; }
    f64& radius_tolerance() noexcept { return radius_tolerance_; }

private:
    const Body*              body_{nullptr};
    const Volume*            volume_{nullptr};
    const RecognitionResult* features_{nullptr};
    f64                      tangent_tolerance_{0.05};
    f64                      radius_tolerance_{1e-6};
};

}
