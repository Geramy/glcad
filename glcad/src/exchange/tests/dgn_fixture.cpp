#include "dgn_fixture.hpp"
#include "dgnlib.h"

namespace gtest {
bool write_dgn_underlay_fixture(const std::string& path,const std::string& master_unit) {
    auto file=DGNCreate(path.c_str(),GLCAD_DGN_SEED,0,0,0,0,1000,100,master_unit.c_str(),"um");
    if(!file)return false;
    const auto write=[&](DGNElemCore* element) {
        if(!element)return false;
        const bool ok=DGNWriteElement(file,element)!=0;DGNFreeElement(file,element);return ok;
    };
    DGNPoint points[]={{-1500,2000,0},{500,2000,0},{500,3000,0},{-1500,3000,0},{-1500,2000,0}};
    auto* shape=DGNCreateMultiPointElem(file,DGNT_SHAPE,5,points);
    if(!shape){DGNClose(file);return false;}
    bool ok=DGNAddShapeFillInfo(file,shape,3)>=0;ok=write(shape)&&ok;
    GByte palette[256][3]{};palette[0][0]=palette[0][1]=palette[0][2]=255;palette[3][0]=255;
    ok=write(DGNCreateColorTableElem(file,0,palette))&&ok;DGNClose(file);return ok;
}
}
