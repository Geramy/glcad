#pragma once

namespace gcapp {

class FrameDemand {
public:
    void request() noexcept { remaining_=2; }
    bool pending(bool work=false) const noexcept { return remaining_>0 || work; }
    bool consume(bool work=false) noexcept {
        if(!pending(work))return false;
        if(remaining_>0)--remaining_;
        return true;
    }
private:
    unsigned remaining_{2};
};

}
