#pragma once
// Sample data for the mock build (SU_MOCK): the UI runs fully offline on this instead of the real API.
#include "api/Models.h"

namespace su::mock {

LeakPage leakPage(int page, int pageSize);
DomainSummary domainSummary();
DarkWebPage darkWebPage(int page, int pageSize);
Usage usage();
Profile profile();
LiveStats liveStats();
IndexStats indexStats();

}  // namespace su::mock
