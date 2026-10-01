#pragma once

#include "Globals.hpp"

#include <hyprland/src/render/pass/PassElement.hpp>

class CRipplePassElement : public IPassElement {
  public:
    struct SData {
        SRipple ripple;
        float   radius = 0.f; // logical, this frame
        float   alpha  = 0.f; // this frame
    };

    explicit CRipplePassElement(const SData& data);
    ~CRipplePassElement() override = default;

    std::vector<UP<IPassElement>> draw() override;
    bool                          needsLiveBlur() override { return false; }
    bool                          needsPrecomputeBlur() override { return false; }
    std::optional<CBox>           boundingBox() override;
    CRegion                       opaqueRegion() override { return {}; }

    const char*      passName() override { return "CRipplePassElement"; }
    ePassElementType type() override { return EK_CUSTOM; }

  private:
    SData m_data;
};

namespace RippleShader {
    // Compiled lazily on first draw (needs the GL context); freed on unload.
    void destroy();
}
