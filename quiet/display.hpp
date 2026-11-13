#pragma once
#include "vendor.hpp"
namespace quiet {
// Display-only cache. It never feeds the control engine or refreshes source time.
class DisplayCache {
    VendorSample last;
public:
    void observe(const VendorSample&sample,uint64_t now){if(vendorFresh(sample,now))last=sample;}
    const VendorSample*reading(const VendorSample&current,uint64_t now)const{
        if(vendorFresh(current,now))return &current;
        return vendorFresh(last,now)?&last:nullptr;
    }
};
}
