#pragma once

#define RTC_DEVELOP (0)
#define RTC_RELEASE (1)

#if CAMP_RELEASE == 1
    #define RTC_TARGET  (RTC_RELEASE)
#else
    #define RTC_TARGET  (RTC_DEVELOP)
#endif


#if RTC_TARGET == RTC_DEVELOP
    #define RTC_DEBUG_CODE(code)    code
#else
    #define RTC_DEBUG_CODE(code)
#endif

