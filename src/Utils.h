#ifndef Utils_h
#define Utils_h
#include <SdFat.h>
#include <asyncHTTPrequest.h>
#include <pb_decode.h>
#include <pb_encode.h>

enum APIEndpoint {
  CONFIG,
  LOGGING,
};

namespace Utils {
bool writeToFile(sdfat::File* file,
                 const pb_msgdesc_t* fields,
                 const void* src_struct);
void configureRequest(asyncHTTPrequest* request, APIEndpoint api);
}  // namespace Utils
#endif /* Utils_h */
